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
      // Явний WiFiClient (_picoWifiClient) замість дефолтного heap-варіанту
      // PicoMQTT - див. коментар біля _picoWifiClient в .hpp. Templated
      // конструктор: Client(ClientType&, host, port, id, user, password,
      // reconnect_interval_millis, keep_alive_millis, socket_timeout_millis).
      , _mqttClient(_picoWifiClient, config.host, config.port, config.clientId,
                  config.username, config.password, 5000, 60000, 5000)
#endif
     {
#if defined(ESP32)
  _networkMutex = xSemaphoreCreateMutex();
  _queueMutex = xSemaphoreCreateMutex();
  _listenersMutex = xSemaphoreCreateMutex();
#if __has_include(<PicoMQTT.h>)
  _outgoingQueueMutex = xSemaphoreCreateMutex();
#endif
#endif
}

MqttClient::~MqttClient() {
#if defined(ESP32)
  _taskShouldRun = false;
  // Даємо таску шанс самому вийти з циклу (див. networkTaskLoop) перш ніж
  // видаляти мьютекси, якими він міг ще користуватись.
  if (_networkTaskHandle != nullptr) {
    vTaskDelay(pdMS_TO_TICKS(50));
    vTaskDelete(_networkTaskHandle);
    _networkTaskHandle = nullptr;
  }
  if (_networkMutex != nullptr) { vSemaphoreDelete(_networkMutex); }
  if (_queueMutex != nullptr) { vSemaphoreDelete(_queueMutex); }
  if (_listenersMutex != nullptr) { vSemaphoreDelete(_listenersMutex); }
#if __has_include(<PicoMQTT.h>)
  if (_outgoingQueueMutex != nullptr) { vSemaphoreDelete(_outgoingQueueMutex); }
#endif
#endif
}

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

#if defined(ESP32)
  // xTaskCreate (без пінінгу) - навмисно, не xTaskCreatePinnedToCore(core=1):
  // ESP32-C6/H2 (RISC-V) - single-core, core=1 там не існує і валить
  // assert() в xTaskCreatePinnedToCore. xTaskCreate сумісний з усіма
  // варіантами (single-core і dual-core), планувальник сам обирає ядро.
  _taskShouldRun = true;
  xTaskCreate(&MqttClient::networkTaskTrampoline, "mqtt-net",
              /*stackSize=*/8192, this, /*priority=*/1, &_networkTaskHandle);
#endif
}

void MqttClient::loop() {
#if defined(ESP32)
  // Мережевий I/O (connect/loop) виконується в networkTaskLoop().
  // Тут лише розбираємо чергу вхідних повідомлень і викликаємо колбеки
  // addListener() - В ГОЛОВНОМУ ПОТОЦІ, безпечно для дисплея/SD/touch
  // логіки всередині колбеків.
  std::vector<MqttIncomingMessage> pending;
  {
    MutexGuard guard(_queueMutex);
    pending.swap(_incomingQueue);
  }
  for (auto& msg : pending) {
    dispatchMessage(msg.topic.c_str(), msg.payload.data(),
                    static_cast<unsigned int>(msg.payload.size()));
  }
#else
  // ESP8266: без змін, кооперативний однопотоковий loop().
  if (!_mqttClient.connected()) {
    uint32_t now = millis();
    if (now - _lastReconnectAttempt >= _config.reconnectIntervalMs) {
      _lastReconnectAttempt = now;
      connect();
    }
    return;
  }
  _mqttClient.loop();
#endif
}

#if defined(ESP32)
void MqttClient::networkTaskTrampoline(void* param) {
  static_cast<MqttClient*>(param)->networkTaskLoop();
}

void MqttClient::networkTaskLoop() {
  while (_taskShouldRun) {
    {
      MutexGuard guard(_networkMutex);

      if (!_mqttClient.connected()) {
        uint32_t now = millis();
        if (now - _lastReconnectAttempt >= _config.reconnectIntervalMs) {
          _lastReconnectAttempt = now;
          connect();
        }
      } else {
        _mqttClient.loop();  // блокуючий виклик - ізольований у власному таску
      }
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
  vTaskDelete(nullptr);
}
#endif

void MqttClient::disconnect(const char* customOfflineMessage) {
#if defined(ESP32) && __has_include(<PubSubClient.h>)
  // ВАЖЛИВО: мьютекс тут лише для PubSubClient-гілки. Для PicoMQTT він
  // навмисно відсутній - емпірично підтверджено, що утримання _networkMutex
  // під час _mqttClient.loop() (яке може тривати до socket_timeout_millis)
  // ламає CONNACK-хендшейк на ESP32-C6 (див. коментар у networkTaskLoop()).
  MutexGuard guard(_networkMutex);
#endif
  const char* message = customOfflineMessage != nullptr ? customOfflineMessage : _config.lwtOfflineMessage;
  bool hasLwtTopic = _config.lwtTopic != nullptr && _config.lwtTopic[0] != '\0';
  bool hasMessage = message != nullptr && message[0] != '\0';

  if (_mqttClient.connected() && hasLwtTopic && hasMessage) {
    std::string lwtTopic = resolveTopic(_config.lwtTopic);
    _mqttClient.publish(lwtTopic.c_str(), message, _config.lwtRetain);
  }
  _mqttClient.disconnect();
}

// УВАГА: викликається і з головного потоку (перша спроба конекту синхронно
// в begin()-стилі старого коду більше немає), і з networkTaskLoop() на ESP32.
// На виклик ззовні (не з networkTaskLoop()) MutexGuard тут НЕ ставимо - лок
// вже тримає викликач (networkTaskLoop бере _networkMutex до виклику connect()).
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

  if (_config.lwtTopic != nullptr && _config.lwtTopic[0] != '\0') {
    // _lwtTopicStorage - член класу, живе весь час роботи клієнта.
    // PicoMQTT читає will.topic при кожній спробі конекту, не лише тут,
    // тому НЕ можна вказувати will.topic на локальний std::string (dangling
    // pointer одразу після виходу з begin()).
    _lwtTopicStorage = resolveTopic(_config.lwtTopic);
    const char* message = _config.lwtOfflineMessage != nullptr ? _config.lwtOfflineMessage : "";

    _mqttClient.will.topic = _lwtTopicStorage.c_str();
    _mqttClient.will.payload = message;
    _mqttClient.will.qos = _config.lwtQos;
    _mqttClient.will.retain = _config.lwtRetain;
  }

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

  // TODO: коли буде підключено per-topic message dispatch для PicoMQTT
  // (наразі не реалізовано - root subscribe на "#" закоментований вище в
  // історії файлу) - НЕ викликати dispatchMessage() напряму з мережевого
  // колбека. Використовувати enqueueIncoming(), як зроблено для PubSubClient
  // в handleMessage() нижче, щоб колбеки addListener() лишались у головному
  // потоці.

  _mqttClient.begin();

#if defined(ESP32)
  // xTaskCreate (без пінінгу) - навмисно, не xTaskCreatePinnedToCore(core=1):
  // ESP32-C6/H2 (RISC-V) - single-core, core=1 там не існує і валить
  // assert() в xTaskCreatePinnedToCore. xTaskCreate сумісний з усіма
  // варіантами (single-core і dual-core), планувальник сам обирає ядро.
  _taskShouldRun = true;
  xTaskCreate(&MqttClient::networkTaskTrampoline, "mqtt-net",
              /*stackSize=*/8192, this, /*priority=*/5, &_networkTaskHandle);
#endif
}

void MqttClient::loop() {
#if defined(ESP32)
  std::vector<MqttIncomingMessage> pending;
  {
    MutexGuard guard(_queueMutex);
    pending.swap(_incomingQueue);
  }
  for (auto& msg : pending) {
    dispatchMessage(msg.topic.c_str(), msg.payload.data(),
                    static_cast<unsigned int>(msg.payload.size()));
  }
#else
  _mqttClient.loop();
#endif
}

#if defined(ESP32)
void MqttClient::networkTaskTrampoline(void* param) {
  static_cast<MqttClient*>(param)->networkTaskLoop();
}

void MqttClient::networkTaskLoop() {
  // ВАЖЛИВО: тут НЕМАЄ MutexGuard(_networkMutex) навколо _mqttClient.loop() -
  // навмисно, підтверджено емпірично. _mqttClient.loop() для PicoMQTT може
  // внутрішньо блокуватись на весь socket_timeout_millis (5с) під час спроби
  // конекту (wait_for_reply). Утримання мьютекса весь цей час, на пріоритеті
  // 5 (вище за головний loop(), пріоритет 1), на single-core ESP32-C6
  // призводило до 100% стабільного провалу CONNACK-хендшейку (available()
  // ніколи не бачив дані від брокера, хоча TCP-сесія встановлювалась) -
  // підтверджено ізольованими тестами: без мьютекса PicoMQTT::Client
  // конектиться миттєво навіть з окремого FreeRTOS-таска; з мьютексом -
  // 100% Connect fail. Ймовірний механізм: тривале утримання мьютекса на
  // високому пріоритеті якимось чином втручається у своєчасну доставку
  // TCP-даних у сокет-буфер на single-core C6.
  //
  // Мережевий таск - ЄДИНИЙ власник _mqttClient для будь-яких операцій
  // (connect/loop/subscribe/unsubscribe/publish). Головний потік НІКОЛИ не
  // звертається до _mqttClient напряму для PicoMQTT - лише кладе команди в
  // _outgoingQueue (drainOutgoingQueue() нижче їх виконує тут). Раніше
  // прямі виклики publish()/subscribe() з головного потоку (без мьютекса,
  // після його видалення через проблему вище) призводили до конкурентного
  // запису в сокет з двох потоків одночасно - це ламало MQTT byte stream
  // на льоту (empірично: SUBSCRIBE на кілька топіків коректно доходив до
  // брокера, потім спотворений запис читався брокером як UNSUBSCRIBE
  // невідповідного топіка, врешті протокол розсинхронізовувався і
  // з'єднання рвалось по socket_timeout).
  while (_taskShouldRun) {
    drainOutgoingQueue();
    _mqttClient.loop();
    vTaskDelay(pdMS_TO_TICKS(10));
  }
  vTaskDelete(nullptr);
}
#endif

void MqttClient::disconnect(const char* customOfflineMessage) {
  const char* message = customOfflineMessage != nullptr ? customOfflineMessage : _config.lwtOfflineMessage;
  bool hasLwtTopic = _config.lwtTopic != nullptr && _config.lwtTopic[0] != '\0';
  bool hasMessage = message != nullptr && message[0] != '\0';

  if (_mqttClient.connected() && hasLwtTopic && hasMessage) {
    std::string lwtTopic = resolveTopic(_config.lwtTopic);
#if defined(ESP32)
    // Через чергу - як і publish()/subscribe() - щоб не писати в сокет
    // конкурентно з мережевим таском (той самий клас проблем, що й
    // publish()/subscribe(), див. коментар у networkTaskLoop()).
    enqueueOutgoing(MqttOutgoingCommand::Type::kPublish, lwtTopic,
                    reinterpret_cast<const uint8_t*>(message), strlen(message), _config.lwtRetain);
#else
    _mqttClient.publish(lwtTopic.c_str(), message, _config.lwtQos, _config.lwtRetain);
#endif
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
#if defined(ESP32)
  MutexGuard guard(_networkMutex);
#endif
  return _mqttClient.publish(fullTopic.c_str(), payload, retained);
#elif __has_include(<PicoMQTT.h>)
#if defined(ESP32)
  enqueueOutgoing(MqttOutgoingCommand::Type::kPublish, fullTopic,
                  reinterpret_cast<const uint8_t*>(payload), strlen(payload), retained);
#else
  _mqttClient.publish(fullTopic.c_str(), payload, 0, retained);
#endif
  return true;
#endif
}

bool MqttClient::publish(const char* topic, const uint8_t* payload, unsigned int length, bool retained) {
  std::string fullTopic = resolveTopic(topic);
#if __has_include(<PubSubClient.h>)
#if defined(ESP32)
  MutexGuard guard(_networkMutex);
#endif
  return _mqttClient.publish(fullTopic.c_str(), payload, length, retained);
#elif __has_include(<PicoMQTT.h>)
#if defined(ESP32)
  enqueueOutgoing(MqttOutgoingCommand::Type::kPublish, fullTopic, payload, length, retained);
#else
  std::string strPayload(reinterpret_cast<const char*>(payload), length);
  _mqttClient.publish(fullTopic.c_str(), strPayload.c_str(), 0, retained);
#endif
  return true;
#endif
}

bool MqttClient::subscribe(const char* topic) {
  std::string fullTopic = resolveTopic(topic);
#if __has_include(<PubSubClient.h>)
#if defined(ESP32)
  MutexGuard guard(_networkMutex);
#endif
  return _mqttClient.subscribe(fullTopic.c_str());
#elif __has_include(<PicoMQTT.h>)
#if defined(ESP32)
  enqueueOutgoing(MqttOutgoingCommand::Type::kSubscribe, fullTopic);
#else
  _mqttClient.subscribe(fullTopic.c_str());
#endif
  return true;
#endif
}

bool MqttClient::unsubscribe(const char* topic) {
  std::string fullTopic = resolveTopic(topic);
#if __has_include(<PubSubClient.h>)
#if defined(ESP32)
  MutexGuard guard(_networkMutex);
#endif
  return _mqttClient.unsubscribe(fullTopic.c_str());
#elif __has_include(<PicoMQTT.h>)
#if defined(ESP32)
  enqueueOutgoing(MqttOutgoingCommand::Type::kUnsubscribe, fullTopic);
#else
  _mqttClient.unsubscribe(fullTopic.c_str());
#endif
  return true;
#endif
}

#if defined(ESP32) && __has_include(<PicoMQTT.h>)
void MqttClient::enqueueOutgoing(MqttOutgoingCommand::Type type, const std::string& topic,
                                 const uint8_t* payload, unsigned int length, bool retained) {
  MqttOutgoingCommand cmd;
  cmd.type = type;
  cmd.topic = topic;
  cmd.retained = retained;
  if (payload != nullptr && length > 0) {
    cmd.payload.assign(payload, payload + length);
  }
  MutexGuard guard(_outgoingQueueMutex);
  _outgoingQueue.push_back(std::move(cmd));
}

// Викликається виключно з networkTaskLoop() (мережевий таск) - єдине місце,
// де _mqttClient.subscribe()/unsubscribe()/publish() реально виконуються
// для PicoMQTT.
void MqttClient::drainOutgoingQueue() {
  std::vector<MqttOutgoingCommand> pending;
  {
    MutexGuard guard(_outgoingQueueMutex);
    pending.swap(_outgoingQueue);
  }
  for (auto& cmd : pending) {
    switch (cmd.type) {
      case MqttOutgoingCommand::Type::kSubscribe:
        _mqttClient.subscribe(cmd.topic.c_str());
        break;
      case MqttOutgoingCommand::Type::kUnsubscribe:
        _mqttClient.unsubscribe(cmd.topic.c_str());
        break;
      case MqttOutgoingCommand::Type::kPublish: {
        std::string strPayload(reinterpret_cast<const char*>(cmd.payload.data()), cmd.payload.size());
        _mqttClient.publish(cmd.topic.c_str(), strPayload.c_str(), 0, cmd.retained);
        break;
      }
    }
  }
}
#endif

// addListener()/removeListener(): _listeners захищений _listenersMutex.
// Фактичний subscribe (для PicoMQTT) - через чергу, виконується мережевим
// таском, а не напряму з головного потоку (див. коментар у networkTaskLoop()).
MqttListenerId MqttClient::addListener(const char* topic, MqttListenerCallback callback) {
  MqttListenerEntry entry;
  entry.id = _nextListenerId++;
  entry.topic = resolveTopic(topic).c_str();
  entry.callback = callback;
  {
#if defined(ESP32)
    MutexGuard guard(_listenersMutex);
#endif
    _listeners.push_back(entry);
  }

  if (_mqttClient.connected()) {
#if __has_include(<PubSubClient.h>)
#if defined(ESP32)
    MutexGuard guard(_networkMutex);
#endif
    _mqttClient.subscribe(entry.topic.c_str());
#elif __has_include(<PicoMQTT.h>)
#if defined(ESP32)
    enqueueOutgoing(MqttOutgoingCommand::Type::kSubscribe, entry.topic.c_str());
#else
    _mqttClient.subscribe(entry.topic.c_str());
#endif
#endif
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
#if defined(ESP32)
  MutexGuard guard(_listenersMutex);
#endif
  for (auto& entry : _listeners) {
    if (entry.id == id) {
      entry.markedForRemoval = true;
      return;
    }
  }
}

void MqttClient::cleanupRemovedListeners() {
#if defined(ESP32)
  MutexGuard guard(_listenersMutex);
#endif
  _listeners.erase(std::remove_if(_listeners.begin(), _listeners.end(),
                     [](const MqttListenerEntry& entry) { return entry.markedForRemoval; }), _listeners.end());
}

// resubscribeAll() викликається або з connect() (тримає _networkMutex), або
// напряму з PicoMQTT connected_callback (мережевий таск, теж під
// _networkMutex - див. networkTaskLoop()). _listenersMutex - окремий м'ютекс,
// тому лок тут безпечний і не конфліктує з _networkMutex.
void MqttClient::resubscribeAll() {
#if defined(ESP32)
  MutexGuard guard(_listenersMutex);
#endif
  for (const auto& entry : _listeners) {
    if (!entry.markedForRemoval) { _mqttClient.subscribe(entry.topic.c_str()); }
  }
}

void MqttClient::dispatchMessage(const char* topic, const uint8_t* payload, unsigned int length) {
  // Копіюємо снепшот під локом, самі callback-и викликаємо без утримання
  // _listenersMutex - callback може викликати addListener()/removeListener()
  // (реентрантність), що призвело б до deadlock на non-recursive mutex.
  std::vector<MqttListenerEntry> snapshot;
  {
#if defined(ESP32)
    MutexGuard guard(_listenersMutex);
#endif
    snapshot = _listeners;
  }
  for (auto& entry : snapshot) {
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

void MqttClient::enqueueIncoming(const char* topic, const uint8_t* payload, unsigned int length) {
#if defined(ESP32)
  MqttIncomingMessage msg;
  msg.topic.assign(topic);
  msg.payload.assign(payload, payload + length);
  MutexGuard guard(_queueMutex);
  _incomingQueue.push_back(std::move(msg));
#endif
}

#if __has_include(<PubSubClient.h>)
void MqttClient::handleMessage(char* topic, uint8_t* payload, unsigned int length) {
#if defined(ESP32)
  // Викликається мережевим таском (з _mqttClient.loop() всередині
  // networkTaskLoop()) - не можна напряму викликати dispatchMessage(),
  // бо колбеки addListener() мають виконуватись у головному потоці.
  enqueueIncoming(topic, payload, length);
#else
  dispatchMessage(topic, payload, length);
#endif
}

void MqttClient::staticCallback(char* topic, uint8_t* payload, unsigned int length) {
  if (_instance != nullptr) { _instance->handleMessage(topic, payload, length); }
}
#endif

#endif // HAS_MQTT_CLIENT
