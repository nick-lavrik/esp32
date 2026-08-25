#include "MqttClient.hpp"
#include <Arduino.h>
#include <Logger.hpp>

#if defined(ESP8266)
#include <ESP8266WiFi.h>
#else
#include <WiFi.h>
#endif

#include <algorithm>
#include "MqttTopicMatcher.hpp"

#if HAS_MQTT_CLIENT

#if __has_include(<PubSubClient.h>) // || true
MqttClient* MqttClient::_instance = nullptr;
#endif

MqttClient::MqttClient(const MqttConfig& config)
    : _config(config), _defaultKeyGenerator(config.prefix)
#if __has_include(<PicoMQTT.h>)
      // Явний WiFiClient (_picoWifiClient) замість дефолтного heap-варіанту
      // PicoMQTT - див. коментар біля _picoWifiClient в .hpp. Templated
      // конструктор: Client(ClientType&, host, port, id, user, password,
      // reconnect_interval_millis, keep_alive_millis, socket_timeout_millis).
      // config.useTls -> той самий templated-конструктор, але з
      // WiFiClientSecure. Налаштування самого TLS (setInsecure/setCACert) -
      // у begin(), бо на момент роботи списку ініціалізації _config ще не
      // гарантовано ініціалізований раніше за цей член.
      , _mqttClient(config.useTls ? static_cast<::Client&>(_picoSecureClient)
                                  : static_cast<::Client&>(_picoWifiClient),
                  config.host, config.port, config.clientId,
                  config.username, config.password, config.reconnectIntervalMs, 60000, 5000)
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
  // useKeyGenerator == false -> топік іде брокеру байт-у-байт (EcoFlow та інші
  // брокери з жорсткою схемою топіків, де провідний '/' значущий).
  if (_config.useKeyGenerator && _keyGenerator != nullptr) {
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
  xTaskCreate(&MqttClient::networkTaskTrampoline,
              _config.taskName != nullptr ? _config.taskName : "mqtt-net",
              _config.taskStackSize, this, /*priority=*/5, &_networkTaskHandle);
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
  reportDroppedMessages();
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
  _connected.store(false, std::memory_order_relaxed);
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
    _connected.store(true, std::memory_order_relaxed);
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
  // _instance = this;

  if (_keyGenerator == nullptr) { _keyGenerator = &_defaultKeyGenerator; }

  if (_config.useTls) {
    // _picoSecureClient уже прив'язаний до _mqttClient у конструкторі -
    // тут лише режим перевірки сертифіката.
    if (_config.caCert != nullptr) {
      _picoSecureClient.setCACert(_config.caCert);
    } else {
      _picoSecureClient.setInsecure();
    }
  }

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
    _connected.store(true, std::memory_order_relaxed);
    Serial.println("[MQTT] Connected to broker successfully!");
    this->resubscribeAll();

    bool hasOnlineMessage = _config.lwtOnlineMessage != nullptr && _config.lwtOnlineMessage[0] != '\0';
    if (!_lwtTopicStorage.empty() && hasOnlineMessage) {
      _mqttClient.publish(_lwtTopicStorage.c_str(), _config.lwtOnlineMessage,
                         _config.lwtQos, _config.lwtRetain);
    }
  };

  _mqttClient.disconnected_callback = [this]() {
    _connected.store(false, std::memory_order_relaxed);
    Serial.println("[MQTT] Disconnected from broker.");
  };

  _mqttClient.connection_failure_callback = [this]() {
    _connected.store(false, std::memory_order_relaxed);
    Serial.println("[MQTT] Connect fail.");
  };

  // TODO: коли буде підключено per-topic message dispatch для PicoMQTT
  // (наразі не реалізовано - root subscribe на "#" закоментований вище в
  // історії файлу) - НЕ викликати dispatchMessage() напряму з мережевого
  // колбека. Використовувати enqueueIncoming(), як зроблено для PubSubClient
  // в handleMessage() нижче, щоб колбеки addListener() лишались у головному
  // потоці.
  // Root-підписка: один топік, далі роздача по addListener()-фільтрах.
  // Для стороннього брокера це НЕ "#" (див. MqttConfig::rootSubscribeTopic).
  const char* rootTopic = _config.rootSubscribeTopic != nullptr ? _config.rootSubscribeTopic : "#";
  _mqttClient.SubscribedMessageListener::subscribe(resolveTopic(rootTopic).c_str(), [this](const char *t, const void* m, const size_t s) {
    this->enqueueIncoming(t, (const uint8_t*)m, s);
    // this->dispatchMessage(t, (const uint8_t*)m, s);
  }, _config.rootSubscribeBufferSize);


  _mqttClient.begin();

#if defined(ESP32)
  startNetworkTask();
#endif
}

#if defined(ESP32)
void MqttClient::startNetworkTask() {
  // Страховка від двох тасків на одному клієнті: попередній міг ще не вийти
  // (suspend() з таймаутом, аварійний шлях).
  const uint32_t start = millis();
  while (_taskRunning && millis() - start < kSuspendTimeoutMs) {
    vTaskDelay(pdMS_TO_TICKS(10));
  }

  // xTaskCreate (без пінінгу) - навмисно, не xTaskCreatePinnedToCore(core=1):
  // ESP32-C6/H2 (RISC-V) - single-core, core=1 там не існує і валить
  // assert() в xTaskCreatePinnedToCore. xTaskCreate сумісний з усіма
  // варіантами (single-core і dual-core), планувальник сам обирає ядро.
  _taskShouldRun = true;
  // priority=1 (не 5!) - навмисно, ЕМПІРИЧНО виявлено: PicoMQTT-таск на
  // priority вищому за головний loop() занадто щільно спінить available_wait()
  // (внутрішній цикл на yield(), без реальних затримок) під час послідовних
  // блокуючих wait_for_reply() викликів (CONNACK, одразу за ним SUBACK у
  // тому самому on_connect()) - це заважає lwIP вчасно доставити вхідні дані
  // на ДРУГОМУ такому виклику поспіль (перший, CONNACK, встигає; SUBACK -
  // ні). Пріоритет 1 (як головний loop()) залишає більше "повітря" для
  // WiFi/lwIP стеку. Якщо це не допоможе - наступний крок: tskIDLE_PRIORITY+1.
  xTaskCreate(&MqttClient::networkTaskTrampoline,
              _config.taskName != nullptr ? _config.taskName : "mqtt-net",
              _config.taskStackSize, this, /*priority=*/tskIDLE_PRIORITY + 1, &_networkTaskHandle);
}

bool MqttClient::suspend() {
  if (_suspended) { return true; }

  // 1. Просимо таск вийти і ЧЕКАЄМО фактичного виходу: доки він живий, він -
  //    єдиний власник _mqttClient, і чіпати клієнт звідси не можна.
  //
  //    Таймаут з ЗАПАСОМ: одна ітерація таска - це _mqttClient.loop(), який
  //    всередині може блокуватись на весь socket_timeout_millis (5 с у
  //    конструкторі) на wait_for_reply, плюс vTaskDelay(100). Колишні 2 с
  //    були МЕНШІ за це вікно, тож під навантаженням suspend() виходив по
  //    таймауту при живому таску - і далі два потоки писали в один сокет.
  _taskShouldRun = false;
  const uint32_t start = millis();
  while (_taskRunning && millis() - start < kSuspendTimeoutMs) {
    vTaskDelay(pdMS_TO_TICKS(10));
  }

  if (_taskRunning) {
    // Аномалія: таск завис глибше, ніж дозволяє socket_timeout. Відкочуємось -
    // хай працює далі, ніж ризикувати гонкою на сокеті.
    _taskShouldRun = true;
    Logger::error("[MQTT] %s: мережевий таск не зупинився за %u мс - suspend скасовано",
                  _config.taskName != nullptr ? _config.taskName : "mqtt",
                  (unsigned)kSuspendTimeoutMs);
    return false;
  }

  _suspended = true;
  _networkTaskHandle = nullptr;  // таск сам зробив vTaskDelete(nullptr)

  // 2. Тепер безпечно рвемо з'єднання з головного потоку.
  _mqttClient.disconnect();
  _connected.store(false, std::memory_order_relaxed);

  // 3. Головне заради чого все: stop() -> stop_ssl_socket() -> mbedtls_ssl_free()
  //    + mbedtls_ssl_config_free(). Без цього буфери сесії лишаються в heap
  //    навіть після disconnect().
  if (_config.useTls) {
    _picoSecureClient.stop();
  } else {
    _picoWifiClient.stop();
  }
  return true;
}

void MqttClient::resume() {
  if (!_suspended) { return; }
  _suspended = false;
  // Слухачі й черги на місці; PicoMQTT сам перепідключиться у своєму loop(),
  // а resubscribeAll() з connected_callback поновить підписки.
  startNetworkTask();
}
#endif

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
  reportDroppedMessages();
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
  _taskRunning = true;
  while (_taskShouldRun) {
    drainOutgoingQueue();
    _mqttClient.loop();
    vTaskDelay(pdMS_TO_TICKS(100));
  }
  _taskRunning = false;  // сигнал для suspend(), що клієнт більше нікому не належить
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

#if !(defined(ESP32) && __has_include(<PicoMQTT.h>))
// Пауза TLS-сесії має сенс лише для ESP32+PicoMQTT (див. коментар у .hpp);
// на решті конфігурацій - явний no-op, щоб виклики лишались переносними.
// true - "паузити нічого": постійної TLS-сесії у власному таску тут немає.
bool MqttClient::suspend() { return true; }
void MqttClient::resume() {}
void MqttClient::startNetworkTask() {}
#endif

bool MqttClient::flushOutgoing(uint32_t timeoutMs) {
#if defined(ESP32) && __has_include(<PicoMQTT.h>)
  const uint32_t start = millis();
  bool empty = false;

  while (true) {
    {
      MutexGuard guard(_outgoingQueueMutex);
      empty = _outgoingQueue.empty();
    }
    if (empty || millis() - start >= timeoutMs) {
      break;
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }

  if (empty) {
    // Черга порожня, але мережевий таск міг забрати її щойно і ще не добити
    // publish у сокет - даємо йому один свій цикл (vTaskDelay(100) в
    // networkTaskLoop()).
    vTaskDelay(pdMS_TO_TICKS(120));
  }
  return empty;
#else
  // PubSubClient / ESP8266: publish виконується синхронно, чекати нічого.
  (void)timeoutMs;
  return true;
#endif
}

void MqttClient::reportDroppedMessages() {
#if defined(ESP32)
  uint32_t incoming = _droppedIncoming.exchange(0, std::memory_order_relaxed);
  uint32_t outgoing = _droppedOutgoing.exchange(0, std::memory_order_relaxed);
  if (incoming > 0 || outgoing > 0) {
    Logger::warn("[MQTT] черга переповнена, відкинуто: %u вхідних, %u вихідних",
                 (unsigned)incoming, (unsigned)outgoing);
  }

  uint32_t denied = _subscribeDenied.exchange(0, std::memory_order_relaxed);
  if (denied > 0) {
    Logger::warn("[MQTT] %s: брокер відхилив %u підписок (SUBACK=0x80) - перевір права акаунта",
                 _config.taskName != nullptr ? _config.taskName : "mqtt", (unsigned)denied);
  }
#endif
}

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
  _mqttClient.PicoMQTT::BasicClient::subscribe(fullTopic.c_str());
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
  _mqttClient.PicoMQTT::BasicClient::unsubscribe(fullTopic.c_str());
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
  // drop-oldest, як і для _incomingQueue: якщо мережевий таск не встигає
  // (немає з'єднання, а головний потік продовжує publish-ити), черга не має
  // з'їдати купу.
  if (_outgoingQueue.size() >= kMaxOutgoingQueue) {
    _outgoingQueue.erase(_outgoingQueue.begin());
    _droppedOutgoing.fetch_add(1, std::memory_order_relaxed);
  }
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
        // Явно кваліфіковано BasicClient:: - без цього виклик резолвиться
        // в PicoMQTT::Client/SubscribedMessageListener::subscribe(topic,
        // callback=nullptr) (high-level API, name hiding ховає
        // BasicClient::subscribe), що лише кладе запис у внутрішню мапу
        // з null-колбеком замість реального мережевого SUBSCRIBE - саме
        // це спричиняло фантомний UNSUBSCRIBE, підтверджено емпірично.
        // _mqttClient.PicoMQTT::BasicClient::subscribe(cmd.topic.c_str());
        _mqttClient.SubscribedMessageListener::subscribe(
          cmd.topic.c_str(),
          [this](const char* t, const void* payload, size_t len) {
            this->enqueueIncoming(t, (const uint8_t*)payload, len);
          },
          2 * 1024);
        break;
      case MqttOutgoingCommand::Type::kUnsubscribe:
        // _mqttClient.PicoMQTT::BasicClient::unsubscribe(cmd.topic.c_str());
        _mqttClient.SubscribedMessageListener::unsubscribe(cmd.topic.c_str());
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
    // Явно кваліфіковано BasicClient:: - див. коментар у drainOutgoingQueue().
    _mqttClient.PicoMQTT::BasicClient::subscribe(entry.topic.c_str());
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

// resubscribeAll() - ВАЖЛИВО: НЕ тримає MutexGuard(_listenersMutex) під час
// самих subscribe()-викликів. Спочатку знімаємо снепшот топіків під локом
// (швидко), відпускаємо мьютекс, і вже потім виконуємо блокуючі subscribe()
// БЕЗ жодного мьютекса. Той самий клас проблем, що й з _networkMutex
// (див. networkTaskLoop()): тримання БУДЬ-ЯКОГО FreeRTOS-мьютекса (навіть
// повністю без конкуренції за нього) під час блокуючого PicoMQTT
// wait_for_reply() ламає читання відповіді на ESP32-C6 - empірично
// підтверджено ізольованим тестом (з MutexGuard - SUBACK ніколи не
// приходить, розрив через socket_timeout; без нього - стабільно).
void MqttClient::resubscribeAll() {
  std::vector<std::string> topicsSnapshot;
  {
#if defined(ESP32)
    MutexGuard guard(_listenersMutex);
#endif
    topicsSnapshot.reserve(_listeners.size());
    for (const auto& entry : _listeners) {
      if (entry.markedForRemoval) { continue; }
      topicsSnapshot.emplace_back(entry.topic.c_str());
    }
  }
  for (const auto& topic : topicsSnapshot) {
#if __has_include(<PubSubClient.h>)
    _mqttClient.subscribe(topic.c_str());
#elif __has_include(<PicoMQTT.h>)
    // Явно кваліфіковано BasicClient:: - див. коментар у drainOutgoingQueue().
    // qos_granted == 0x80 - брокер ВІДХИЛИВ підписку (ACL); мовчки ковтати це
    // не можна, інакше клієнт виглядає справним і просто нічого не отримує.
    uint8_t qosGranted = 0;
    bool accepted = _mqttClient.PicoMQTT::BasicClient::subscribe(topic.c_str(), 0, &qosGranted);
    if (!accepted || qosGranted == 0x80) {
      _subscribeDenied.fetch_add(1, std::memory_order_relaxed);
    }
#endif
  }
}

void MqttClient::dispatchMessage(const char* topic, const uint8_t* payload, unsigned int length) {
  // Матчинг топіка робимо ПІД локом (MqttTopicMatcher::match - чиста й дешева
  // функція), а копіюємо лише колбеки тих слухачів, що реально підійшли -
  // зазвичай нуль або один. Раніше тут копіювався ВЕСЬ _listeners (String +
  // std::function на кожен запис) на КОЖНЕ повідомлення; при підписці на "#"
  // це десятки алокацій на секунду.
  //
  // Копія колбека все одно потрібна: викликати його під локом не можна -
  // колбек може сам звернутись до addListener()/removeListener(), а це
  // deadlock на нерекурсивному мьютексі.
  //
  // _dispatchScratch - член класу, щоб переюзати вже виділену capacity між
  // викликами. Викликається лише з loop() (головний потік) і не реентрантний.
  _dispatchScratch.clear();
  {
#if defined(ESP32)
    MutexGuard guard(_listenersMutex);
#endif
    for (const auto& entry : _listeners) {
      if (entry.markedForRemoval || !entry.callback) { continue; }
      if (!MqttTopicMatcher::match(entry.topic.c_str(), topic)) { continue; }
      _dispatchScratch.push_back(entry.callback);
    }
  }

  for (auto& callback : _dispatchScratch) {
    callback(topic, payload, length);
  }
  _dispatchScratch.clear();  // не тримаємо копії колбеків між викликами

  cleanupRemovedListeners();
}

/* bool MqttClient::isConnected() const {
#if __has_include(<PubSubClient.h>)
  return const_cast<PubSubClient&>(_mqttClient).connected();
#elif __has_include(<PicoMQTT.h>)
  return const_cast<PicoMQTT::Client&>(_mqttClient).connected();
#endif
} */

void MqttClient::enqueueIncoming(const char* topic, const uint8_t* payload, unsigned int length) {
#if defined(ESP32)
  MqttIncomingMessage msg;
  msg.topic.assign(topic);
  msg.payload.assign(payload, payload + length);

  MutexGuard guard(_queueMutex);
  // drop-oldest: краще втратити найстаріше повідомлення, ніж купу (див.
  // kMaxIncomingQueue). Про факт втрати повідомить loop() головного потоку.
  if (_incomingQueue.size() >= kMaxIncomingQueue) {
    _incomingQueue.erase(_incomingQueue.begin());
    _droppedIncoming.fetch_add(1, std::memory_order_relaxed);
  }
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
