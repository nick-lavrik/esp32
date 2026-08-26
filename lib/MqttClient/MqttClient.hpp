#pragma once

#if __has_include(<PubSubClient.h>) || __has_include(<PicoMQTT.h>)
#define HAS_MQTT_CLIENT 1
#include <ArduinoJson.h>

#if __has_include(<PubSubClient.h>)
#include <PubSubClient.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#elif __has_include(<PicoMQTT.h>)
#include <PicoMQTT.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#endif

#if defined(ESP32)
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#endif

#include <atomic>
#include <cstring>
#include <string>
#include <type_traits>
#include <vector>

#include "MqttConfig.hpp"
#include "MqttKeyGenerator.hpp"
#include "MqttListenerEntry.hpp"

// Вхідне повідомлення з мережевого таска в чергу для розбору в MqttClient::loop()
// (тобто в контексті головного sketch loop(), а не мережевого таска).
struct MqttIncomingMessage {
  std::string topic;
  std::vector<uint8_t> payload;
};

// Вихідна команда з головного потоку в чергу для виконання мережевим таском
// (тільки PicoMQTT-гілка - мережевий таск лишається ЄДИНИМ власником
// _mqttClient; без цього publish()/subscribe() з головного потоку писали б
// в сокет конкурентно з мережевим таском, що на практиці ламало MQTT-протокол
// на ESP32-C6 - empirично підтверджено).
struct MqttOutgoingCommand {
  enum class Type { kSubscribe, kUnsubscribe, kPublish };
  Type type;
  std::string topic;
  std::vector<uint8_t> payload;  // лише для kPublish
  bool retained = false;
};

class MqttClient {
public:
  explicit MqttClient(const MqttConfig& config);
  ~MqttClient();

  void setKeyGenerator(MqttKeyGenerator* keyGenerator);
  const MqttKeyGenerator& keyGenerator() const;

  // На ESP32: begin() піднімає окремий FreeRTOS-таск для connect()/loop()
  // мережевого рівня (щоб retransmission-затримки не блокували sketch loop()).
  // loop() потрібно й надалі викликати з sketch loop() - він лише розбирає
  // чергу вхідних повідомлень і виконує колбеки addListener() в головному
  // потоці (безпечно для дисплея/SD/touch логіки всередині колбеків).
  // На ESP8266: поведінка без змін (кооперативний loop(), без окремого таска).
  void begin();
  void loop();
  void disconnect(const char* customOfflineMessage = nullptr);

  // Чекає (з викликаючого потоку), доки мережевий таск не вижене чергу
  // вихідних команд, але не довше timeoutMs. true - черга порожня.
  //
  // Потрібно перед ESP.restart(): у PicoMQTT-гілці publish()/disconnect() лише
  // КЛАДУТЬ команду в _outgoingQueue, а виконує її мережевий таск раз на
  // ~100 мс. Без цього очікування offline-LWT перед ребутом фізично не встигав
  // піти до брокера. Для PubSubClient-гілки та ESP8266 - завжди true (там
  // publish синхронний).
  bool flushOutgoing(uint32_t timeoutMs = 500);

  // Тимчасово знімає мережевий таск і РВЕ TLS-сесію, звільняючи mbedTLS-буфери
  // (~32 КБ heap на сесію: CONFIG_MBEDTLS_SSL_MAX_CONTENT_LEN=16384 на кожен
  // напрямок). Потрібно, бо ESP32-C6 не тягне дві одночасні TLS-сесії: спроба
  // підняти HTTPS поверх живого MQTT-over-TLS падає в mbedTLS з
  // "BIGNUM - Memory allocation failed" ще на RSA-операції хендшейку.
  //
  // Черги повідомлень і список слухачів зберігаються - resume() перепідключає
  // клієнт і повторно підписується (resubscribeAll() з connected_callback).
  // Лише ESP32 + PicoMQTT; на інших конфігураціях - no-op.
  // Повертає false, якщо мережевий таск не завершився у відведений час: тоді
  // клієнт НЕ чіпається і лишається працювати. Краще відмовити у REST, ніж
  // писати в сокет одночасно з живим таском - це ламає MQTT-потік намертво.
  bool suspend();
  void resume();
  bool isSuspended() const { return _suspended; }

  // Скільки байтів стеку мережевого таска НЕ використано в найгірший момент.
  // Потрібно, щоб підбирати MqttConfig::taskStackSize за фактом, а не навмання:
  // TLS-хендшейк - найглибше місце, і промах тут = крах у мережевому таску.
  // 0, якщо таска немає (не ESP32 або клієнт на паузі).
  size_t networkTaskStackHeadroom() const;

  bool publish(const char* topic, const char* payload, bool retained = false);
  bool publish(const char* topic, const uint8_t* payload, unsigned int length, bool retained = false);
  bool subscribe(const char* topic);
  bool unsubscribe(const char* topic);

  MqttListenerId addListener(const char* topic, MqttListenerCallback callback);
  MqttListenerId addStringListener(const char* topic, MqttStringListenerCallback callback);
  void removeListener(MqttListenerId id);

  // --- Шаблони для POD / Struct (Залишаються без змін) ---
  template <typename T>
  bool publishStruct(const char* topic, const T& value, bool retained = false) {
    static_assert(std::is_trivially_copyable<T>::value, "T must be POD (trivially copyable)");
    return publish(topic, reinterpret_cast<const uint8_t*>(&value), sizeof(T), retained);
  }

  template <typename T>
  MqttListenerId addStructListener(const char* topic, std::function<void(const char*, const T&)> callback) {
    static_assert(std::is_trivially_copyable<T>::value, "T must be POD (trivially copyable)");
    return addListener(topic, [callback](const char* topic, const uint8_t* payload, unsigned int length) -> void {
          T value{};
          unsigned int copyLength = length < sizeof(T) ? length : sizeof(T);
          memcpy(&value, payload, copyLength);
          callback(topic, value);
        });
  }

  template <typename T>
  bool publishNumber(const char* topic, T value, bool retained = false, uint8_t precision = 2) {
    static_assert(std::is_arithmetic<T>::value, "T must be an arithmetic type");
    char buffer[32];
    if constexpr (std::is_floating_point<T>::value) {
      snprintf(buffer, sizeof(buffer), "%.*f", precision, static_cast<double>(value));
    } else if constexpr (std::is_signed<T>::value) {
      snprintf(buffer, sizeof(buffer), "%lld", static_cast<long long>(value));
    } else {
      snprintf(buffer, sizeof(buffer), "%llu", static_cast<unsigned long long>(value));
    }
    return publish(topic, buffer, retained);
  }

  template <typename T>
  MqttListenerId addNumberListener(const char* topic, std::function<void(const char*, T)> callback) {
    static_assert(std::is_arithmetic<T>::value, "T must be an arithmetic type");
    return addStringListener(topic, [callback](const char* topic, const char* payload) -> void {
      T value;
      if constexpr (std::is_floating_point<T>::value) { value = static_cast<T>(strtod(payload, nullptr)); }
      else if constexpr (std::is_signed<T>::value) { value = static_cast<T>(strtoll(payload, nullptr, 10)); }
      else { value = static_cast<T>(strtoull(payload, nullptr, 10)); }
      callback(topic, value);
    });
  }

  bool publishJson(const char* topic, JsonDocument& doc, bool retained = false);
  MqttListenerId addJsonListener(const char* topic, std::function<void(const char*, JsonDocument&)> callback);

  bool isConnected() const { return _connected; };

private:
  std::atomic<bool> _connected{false};

  // Спільне для всіх платформ, бо isSuspended() публічний: на конфігураціях без
  // паузи (не-ESP32 / не-PicoMQTT) suspend() - no-op, і прапорець просто завжди
  // лишається false.
  bool _suspended = false;

  bool connect();
  void resubscribeAll();
  void dispatchMessage(const char* topic, const uint8_t* payload, unsigned int length);
  void cleanupRemovedListeners();
  void enqueueIncoming(const char* topic, const uint8_t* payload, unsigned int length);

  // Логує (з головного потоку) факт відкидання повідомлень через переповнення
  // черг і скидає лічильники. Викликається з loop().
  void reportDroppedMessages();

  std::string resolveTopic(const char* topic) const;

  MqttConfig _config;
  MqttKeyGenerator _defaultKeyGenerator;
  MqttKeyGenerator* _keyGenerator = nullptr;

#if __has_include(<PicoMQTT.h>)
  // Сховище для will.topic: PicoMQTT::Client зберігає лише const char*
  // (не копіює рядок), а will.topic читається при КОЖНІЙ спробі конекту,
  // не тільки в begin(). Тому резолвлений (з префіксом) topic має жити
  // весь час роботи клієнта - не можна віддавати вказівник на локальний
  // std::string, який знищується одразу після begin().
  std::string _lwtTopicStorage;
#endif

#if __has_include(<PubSubClient.h>) // || true
  static MqttClient* _instance;
#endif

#if __has_include(<PubSubClient.h>)
  WiFiClient _plainClient;
  WiFiClientSecure _secureClient;
  PubSubClient _mqttClient;

  void handleMessage(char* topic, uint8_t* payload, unsigned int length);
  static void staticCallback(char* topic, uint8_t* payload, unsigned int length);
#elif __has_include(<PicoMQTT.h>)
  // ВАЖЛИВО: явно володіємо WiFiClient і передаємо його PicoMQTT::Client
  // через templated-конструктор (Client(ClientType&, host, ...)), а НЕ
  // через дефолтний Client(host, ...) конструктор. Дефолтний варіант сам
  // створює WiFiClient через `new ClientSocket<::WiFiClient>()` (купа,
  // прихована за unique_ptr<ClientSocketInterface>) - саме з ЦИМ шляхом
  // на ESP32-C6 CONNACK ніколи не долітав до available()/read(), хоча
  // сирий WiFiClient (стековий об'єкт, прямий connect()) читав CONNACK
  // миттєво. Причина різниці не з'ясована до кінця (ймовірно щось у
  // внутрішньому proxy/буферизації PicoMQTT для heap-варіанту на C6),
  // але явний WiFiClient-член - робочий обхідний шлях, підтверджений
  // ізольованим тестом.
  WiFiClient _picoWifiClient;
  // Окремий TLS-сокет для config.useTls. PicoMQTT приймає будь-який ::Client
  // через templated-конструктор, тому WiFiClientSecure підставляється замість
  // WiFiClient без змін у самій бібліотеці. Обидва члени існують завжди
  // (обираємо один у конструкторі) - інакше довелось би розводити типи
  // шаблоном по всьому класу. Ціна - зайвий порожній WiFiClientSecure у
  // plain-режимі: він не робить нічого, доки не викликано connect().
  WiFiClientSecure _picoSecureClient;
  PicoMQTT::Client _mqttClient;
#endif

  std::vector<MqttListenerEntry> _listeners;
  MqttListenerId _nextListenerId = 1;
  uint32_t _lastReconnectAttempt = 0;

  // Переюзний буфер під колбеки, що підійшли під топік - див. dispatchMessage().
  std::vector<MqttListenerCallback> _dispatchScratch;

  // Ліміти черг (drop-oldest при переповненні) - та сама політика, що в
  // PrintQueue/ScreenLogTail.
  //
  // Без ліміту _incomingQueue росла безмежно: PicoMQTT-гілка підписується на
  // "#", тобто мережевий таск кладе в чергу КОЖНЕ повідомлення брокера, а
  // розбирає її лише loop() головного потоку. Достатньо однієї затримки в
  // loop() (ефект зображення, "status sd", блокуючий doPing()) під потоком
  // повідомлень - і купа закінчувалась.
  // Скільки чекати виходу мережевого таска в suspend(). Має бути помітно
  // більшим за socket_timeout_millis (5 с), інакше suspend() повертається при
  // ЖИВОМУ таску - див. коментар у MqttClient::suspend().
  static constexpr uint32_t kSuspendTimeoutMs = 10000;

  static constexpr size_t kMaxIncomingQueue = 32;
  static constexpr size_t kMaxOutgoingQueue = 32;

#if defined(ESP32)
  // --- Потокобезпека для мережевого таска (тільки ESP32) ---
  // _networkMutex захищає _mqttClient - АЛЕ ТІЛЬКИ для PubSubClient-гілки
  // (див. коментарі біля кожного MutexGuard(_networkMutex) нижче). Для
  // PicoMQTT мережевий таск - ЄДИНИЙ власник _mqttClient: жодних прямих
  // звернень з головного потоку. Замість цього publish()/subscribe()/
  // unsubscribe() з головного потоку кладуть команду в _outgoingQueue
  // (захищена _outgoingQueueMutex), яку розбирає сам мережевий таск у
  // своєму циклі. Без цього конкурентні записи в сокет з двох потоків
  // ламали MQTT-протокол на ESP32-C6 (empірично підтверджено).
  // _queueMutex захищає лише _incomingQueue.
  // _listeners захищений окремим _listenersMutex (див. нижче) - до нього
  // звертаються і головний потік (addListener/removeListener/dispatchMessage),
  // і мережевий таск (resubscribeAll() з connected_callback).
  SemaphoreHandle_t _networkMutex = nullptr;
  SemaphoreHandle_t _queueMutex = nullptr;
  // _listenersMutex захищає _listeners: addListener()/removeListener()
  // (головний потік) можуть виконуватись одночасно з resubscribeAll()
  // (мережевий таск, спрацьовує з connected_callback) або dispatchMessage()
  // (головний потік, з loop()) - без цього мьютекса це data race на
  // std::vector.
  SemaphoreHandle_t _listenersMutex = nullptr;
  TaskHandle_t _networkTaskHandle = nullptr;
  std::vector<MqttIncomingMessage> _incomingQueue;

  // Скільки повідомлень/команд відкинуто через переповнення черг. Інкрементує
  // мережевий таск, читає й скидає loop() головного потоку - логувати з
  // мережевого таска не хочемо (там навмисно немає ні мьютексів логера, ні
  // блокуючих викликів).
  std::atomic<uint32_t> _droppedIncoming{0};
  std::atomic<uint32_t> _droppedOutgoing{0};

  // Скільки SUBSCRIBE брокер відхилив (SUBACK = 0x80). Без цього лічильника
  // відмова за ACL виглядає як повна тиша при isConnected() == true - клієнт
  // "підключений", підписки прийняті на вигляд, а повідомлень нема. Саме так
  // поводиться EcoFlow, коли accessKey відкликано: конект проходить, права -
  // порожні. Інкрементує мережевий таск, читає й скидає loop().
  std::atomic<uint32_t> _subscribeDenied{0};

#if __has_include(<PicoMQTT.h>)
  // Черга вихідних команд (тільки PicoMQTT) - див. коментар вище біля
  // _networkMutex. Розбирається виключно в networkTaskLoop().
  SemaphoreHandle_t _outgoingQueueMutex = nullptr;
  std::vector<MqttOutgoingCommand> _outgoingQueue;
  void enqueueOutgoing(MqttOutgoingCommand::Type type, const std::string& topic,
                       const uint8_t* payload = nullptr, unsigned int length = 0,
                       bool retained = false);
  void drainOutgoingQueue();
#endif

  volatile bool _taskShouldRun = false;
  // Ставиться самим мережевим таском: true на вході в networkTaskLoop(), false
  // перед vTaskDelete(nullptr). Дозволяє suspend() дочекатися РЕАЛЬНОГО виходу
  // таска, перш ніж чіпати _mqttClient з головного потоку (інакше два потоки
  // одночасно писали б у сокет - див. коментар у networkTaskLoop()).
  volatile bool _taskRunning = false;

  // Спільна частина begin()/resume(): піднімає мережевий таск.
  void startNetworkTask();

  static void networkTaskTrampoline(void* param);
  void networkTaskLoop();

  // RAII-хелпер для лока/анлока SemaphoreHandle_t.
  struct MutexGuard {
    explicit MutexGuard(SemaphoreHandle_t mutex) : _mutex(mutex) {
      if (_mutex != nullptr) { xSemaphoreTake(_mutex, portMAX_DELAY); }
    }
    ~MutexGuard() {
      if (_mutex != nullptr) { xSemaphoreGive(_mutex); }
    }
    SemaphoreHandle_t _mutex;
  };
#endif
};

#else
#define HAS_MQTT_CLIENT 0
#endif
