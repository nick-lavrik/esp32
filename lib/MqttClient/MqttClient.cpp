#include "MqttClient.hpp"
#include <Arduino.h>

#if defined(ESP8266)
#include <ESP8266WiFi.h>
#else
#include <WiFi.h>
#endif

#include <algorithm>
#include "MqttTopicMatcher.hpp"

#if HAS_MQTT_CLIENT

#if __has_include(<PubSubClient.h>)
MqttClient* MqttClient::_instance = nullptr;
#endif

MqttClient::MqttClient(const MqttConfig& config)
    : _config(config), _defaultKeyGenerator(config.prefix)
#if __has_include(<PicoMQTT.h>)
      // Передаємо таймаут 1500мс (1.5 сек) замість 10 сек
      /* , _mqttClient(config.host, config.port, config.clientId, 
                  config.username, config.password, 5000, 30000, 1500)  */
#endif
     {}

void MqttClient::setKeyGenerator(MqttKeyGenerator* keyGenerator) { _keyGenerator = keyGenerator; }

const MqttKeyGenerator& MqttClient::keyGenerator() const {
  return _keyGenerator != nullptr ? *_keyGenerator : _defaultKeyGenerator;
}

std::string MqttClient::resolveTopic(const char* topic) const {
  if (_keyGenerator != nullptr) {
    return _keyGenerator->key(topic);
  }
  return topic != nullptr ? std::string(topic) : std::string();
}

// ==========================================
// НАЛАШТУВАННЯ ДЛЯ PUBSUBCLIENT
// ==========================================
#if __has_include(<PubSubClient.h>)

void MqttClient::begin() {
  _instance = this;
  if (_keyGenerator == nullptr) { _keyGenerator = &_defaultKeyGenerator; }

  if (_config.useTls) {
    if (_config.caCert != nullptr) {
#if BOARD_ESP8266
      _secureClient.setInsecure();
#else
      _secureClient.setCACert(_config.caCert);
#endif
    } else {
      _secureClient.setInsecure();
    }
    _mqttClient.setClient(_secureClient);
  } else {
    _mqttClient.setClient(_plainClient);
  }

  _mqttClient.setSocketTimeout(1);
  _mqttClient.setKeepAlive(60);
  _mqttClient.setServer(_config.host, _config.port);
  _mqttClient.setBufferSize(_config.bufferSize);
  _mqttClient.setCallback(MqttClient::staticCallback);
}

void MqttClient::loop() {
  if (!_mqttClient.connected()) {
    uint32_t now = millis();
    if (now - _lastReconnectAttempt >= _config.reconnectIntervalMs) {
      _lastReconnectAttempt = now;
      connect();
    }
    return;
  }
  _mqttClient.loop();
}

void MqttClient::disconnect(const char* customOfflineMessage) {
  const char* message = customOfflineMessage != nullptr ? customOfflineMessage : _config.lwtOfflineMessage;
  bool hasLwtTopic = _config.lwtTopic != nullptr && _config.lwtTopic[0] != '\0';
  bool hasMessage = message != nullptr && message[0] != '\0';

  if (_mqttClient.connected() && hasLwtTopic && hasMessage) {
    std::string lwtTopic = resolveTopic(_config.lwtTopic);
    _mqttClient.publish(lwtTopic.c_str(), message, _config.lwtRetain);
  }
  _mqttClient.disconnect();
}

bool MqttClient::connect() {
  bool hasLwtTopic = _config.lwtTopic != nullptr && _config.lwtTopic[0] != '\0';
  bool hasOfflineMessage = _config.lwtOfflineMessage != nullptr && _config.lwtOfflineMessage[0] != '\0';
  bool hasOnlineMessage = _config.lwtOnlineMessage != nullptr && _config.lwtOnlineMessage[0] != '\0';
  bool hasLwt = hasLwtTopic && hasOfflineMessage;

  std::string lwtTopic = hasLwtTopic ? resolveTopic(_config.lwtTopic) : std::string();

  bool ok;
  if (hasLwt) {
    ok = _config.useAuth ? _mqttClient.connect(_config.clientId, _config.username, _config.password,
                                               lwtTopic.c_str(), _config.lwtQos, _config.lwtRetain,
                                               _config.lwtOfflineMessage)
                         : _mqttClient.connect(_config.clientId, lwtTopic.c_str(), _config.lwtQos,
                                               _config.lwtRetain, _config.lwtOfflineMessage);
  } else {
    ok = _config.useAuth ? _mqttClient.connect(_config.clientId, _config.username, _config.password)
                         : _mqttClient.connect(_config.clientId);
  }

  if (ok) {
    resubscribeAll();
    if (hasLwtTopic && hasOnlineMessage) {
      _mqttClient.publish(lwtTopic.c_str(), _config.lwtOnlineMessage, _config.lwtRetain);
    }
  }
  return ok;
}
#endif

// ==========================================
// НАЛАШТУВАННЯ ДЛЯ PICOMQTT
// ==========================================
#if __has_include(<PicoMQTT.h>)

void MqttClient::begin() {
  if (_keyGenerator == nullptr) { _keyGenerator = &_defaultKeyGenerator; }

  // Базова конфігурація клієнта
  _mqttClient.host = _config.host;
  _mqttClient.port = _config.port;
  _mqttClient.client_id = _config.clientId;

  if (_config.useAuth) {
    _mqttClient.username = _config.username;
    _mqttClient.password = _config.password;
  }

  // 2. Жорстко обмежуємо таймаут сокета. За замовчуванням там 10 секунд,
  // ми ставимо 1.5 секунди, щоб loop() за будь-яких умов працював плавно.
  // _mqttClient.socket_timeout_millis = 1500;
  // _mqttClient.keep_alive_millis = 30000;

  // ВИПРАВЛЕНО LWT: Заповнюємо вбудовану структуру `will` поелементно
  if (_config.lwtTopic != nullptr && _config.lwtTopic[0] != '\0') {
    std::string lwtTopic = resolveTopic(_config.lwtTopic);
    const char* message = _config.lwtOfflineMessage != nullptr ? _config.lwtOfflineMessage : "";
    
    // Пряме копіювання значень у поля вбудованого об'єкта `will`
    _mqttClient.will.topic = lwtTopic.c_str();
    _mqttClient.will.payload = message;
    _mqttClient.will.qos = _config.lwtQos;
    _mqttClient.will.retain = _config.lwtRetain;
  }

  // ГЛОБАЛЬНИЙ КОЛБЕК (Залишається без змін, вичитує Stream пакет)
  /* std::string root = _keyGenerator->key("#");
  _mqttClient.subscribe(root.c_str(), [this](const char* topic, PicoMQTT::IncomingPacket& packet) {
    size_t length = packet.get_remaining_size();
    std::vector<uint8_t> buffer(length);
    packet.read(buffer.data(), length);

    // Перенаправляємо в основну систему лістенерів проєкту
    this->dispatchMessage(topic, buffer.data(), length);
  }); */

  // ВИДЛЕНО СУБСКРАЙБ НА "#". Замість цього використовуємо рідний default_callback бібліотеки.
  // Він асинхронно ловитиме пакети ТІЛЬКИ з тих топіків, на які ви реально підписалися через addListener().
  /* _mqttClient.default_callback = [this](const char* topic, PicoMQTT::IncomingPacket& packet) {
    size_t length = packet.get_remaining_size();
    std::vector<uint8_t> buffer(length);
    packet.read(buffer.data(), length);

    this->dispatchMessage(topic, buffer.data(), length);
  }; */

  // 4. КОЛБЕК З’ЄДНАННЯ: Щойно PicoMQTT успішно авторизується на брокері, 
  // ми перепідписуємо всі ваші додані лістенери
  _mqttClient.connected_callback = [this]() {
    Serial.println("[MQTT] Connected to broker successfully!");
    this->resubscribeAll();
  };

  _mqttClient.disconnected_callback = []() {
    Serial.println("[MQTT] Disconnected from broker.");
  };

  _mqttClient.connection_failure_callback = []() {
    Serial.println("[MQTT] Connect fail.");
  };

  _mqttClient.begin();
}

void MqttClient::loop() {
  _mqttClient.loop(); // Виконується миттєво, не блокує процесор
}

void MqttClient::disconnect(const char* customOfflineMessage) {
  const char* message = customOfflineMessage != nullptr ? customOfflineMessage : _config.lwtOfflineMessage;
  bool hasLwtTopic = _config.lwtTopic != nullptr && _config.lwtTopic[0] != '\0';
  bool hasMessage = message != nullptr && message[0] != '\0';

  if (_mqttClient.connected() && hasLwtTopic && hasMessage) {
    std::string lwtTopic = resolveTopic(_config.lwtTopic);
    _mqttClient.publish(lwtTopic.c_str(), message, _config.lwtQos, _config.lwtRetain);
  }
}

bool MqttClient::connect() {
  return _mqttClient.connected();
}
#endif

// ==========================================
// СПІЛЬНІ МЕТОДИ ДЛЯ ПУБЛІКАЦІЙ ТА ЛІСТЕНЕРІВ
// ==========================================

bool MqttClient::publish(const char* topic, const char* payload, bool retained) {
  std::string fullTopic = resolveTopic(topic);
#if __has_include(<PubSubClient.h>)
  return _mqttClient.publish(fullTopic.c_str(), payload, retained);
#elif __has_include(<PicoMQTT.h>)
  _mqttClient.publish(fullTopic.c_str(), payload, 0, retained);
  return true;
#endif
}

bool MqttClient::publish(const char* topic, const uint8_t* payload, unsigned int length, bool retained) {
  std::string fullTopic = resolveTopic(topic);
#if __has_include(<PubSubClient.h>)
  return _mqttClient.publish(fullTopic.c_str(), payload, length, retained);
#elif __has_include(<PicoMQTT.h>)
  std::string strPayload(reinterpret_cast<const char*>(payload), length);
  _mqttClient.publish(fullTopic.c_str(), strPayload.c_str(), 0, retained);
  return true;
#endif
}

bool MqttClient::subscribe(const char* topic) {
  std::string fullTopic = resolveTopic(topic);
#if __has_include(<PubSubClient.h>)
  return _mqttClient.subscribe(fullTopic.c_str());
#elif __has_include(<PicoMQTT.h>)
  _mqttClient.subscribe(fullTopic.c_str());
  return true;
#endif
}

bool MqttClient::unsubscribe(const char* topic) {
  std::string fullTopic = resolveTopic(topic);
#if __has_include(<PubSubClient.h>)
  return _mqttClient.unsubscribe(fullTopic.c_str());
#elif __has_include(<PicoMQTT.h>)
  _mqttClient.unsubscribe(fullTopic.c_str());
  return true;
#endif
}

MqttListenerId MqttClient::addListener(const char* topic, MqttListenerCallback callback) {
  MqttListenerEntry entry;
  entry.id = _nextListenerId++;
  entry.topic = resolveTopic(topic).c_str();
  entry.callback = callback;
  _listeners.push_back(entry);

  if (_mqttClient.connected()) {
    _mqttClient.subscribe(entry.topic.c_str());
  }
  return entry.id;
}

bool MqttClient::publishJson(const char* topic, JsonDocument& doc, bool retained) {
  size_t size = measureJson(doc);
  std::vector<char> buffer(size + 1);
  serializeJson(doc, buffer.data(), buffer.size());
  return publish(topic, reinterpret_cast<const uint8_t*>(buffer.data()), size, retained);
}

MqttListenerId MqttClient::addJsonListener(const char* topic, std::function<void(const char*, JsonDocument&)> callback) {
  return addListener(topic, [callback](const char* topic, const uint8_t* payload, unsigned int length) -> void {
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, payload, length);
        if (error) { return; }
        callback(topic, doc);
      });
}

MqttListenerId MqttClient::addStringListener(const char* topic, MqttStringListenerCallback callback) {
  return addListener(topic, [callback](const char* messageTopic, const uint8_t* payload, unsigned int length) -> void {
        std::vector<char> buffer(length + 1);
        memcpy(buffer.data(), payload, length);
        buffer[length] = '\0';
        callback(messageTopic, buffer.data());
      });
}

void MqttClient::removeListener(MqttListenerId id) {
  for (auto& entry : _listeners) {
    if (entry.id == id) {
      entry.markedForRemoval = true;
      return;
    }
  }
}

void MqttClient::cleanupRemovedListeners() {
  _listeners.erase(std::remove_if(_listeners.begin(), _listeners.end(),
                     [](const MqttListenerEntry& entry) { return entry.markedForRemoval; }), _listeners.end());
}

void MqttClient::resubscribeAll() {
  for (const auto& entry : _listeners) {
    if (!entry.markedForRemoval) { _mqttClient.subscribe(entry.topic.c_str()); }
  }
}

void MqttClient::dispatchMessage(const char* topic, const uint8_t* payload, unsigned int length) {
  for (auto& entry : _listeners) {
    if (entry.markedForRemoval || !entry.callback) { continue; }
    if (!MqttTopicMatcher::match(entry.topic.c_str(), topic)) { continue; }
    entry.callback(topic, payload, length);
  }
  cleanupRemovedListeners();
}

bool MqttClient::isConnected() const {
#if __has_include(<PubSubClient.h>)
  return const_cast<PubSubClient&>(_mqttClient).connected();
#elif __has_include(<PicoMQTT.h>)
  return const_cast<PicoMQTT::Client&>(_mqttClient).connected();
#endif
}

#if __has_include(<PubSubClient.h>)
void MqttClient::handleMessage(char* topic, uint8_t* payload, unsigned int length) {
  dispatchMessage(topic, payload, length);
}

void MqttClient::staticCallback(char* topic, uint8_t* payload, unsigned int length) {
  if (_instance != nullptr) { _instance->handleMessage(topic, payload, length); }
}
#endif

#endif // HAS_MQTT_CLIENT
