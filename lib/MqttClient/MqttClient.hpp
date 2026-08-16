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
#endif

#if defined(ESP32)
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#endif

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
    static_assert(std::is_trivially_copyable<T>::value, "T має бути POD (trivially copyable)");
    return publish(topic, reinterpret_cast<const uint8_t*>(&value), sizeof(T), retained);
  }

  template <typename T>
  MqttListenerId addStructListener(const char* topic, std::function<void(const char*, const T&)> callback) {
    static_assert(std::is_trivially_copyable<T>::value, "T має бути POD (trivially copyable)");
    return addListener(topic, [callback](const char* topic, const uint8_t* payload, unsigned int length) -> void {
          T value{};
          unsigned int copyLength = length < sizeof(T) ? length : sizeof(T);
          memcpy(&value, payload, copyLength);
          callback(topic, value);
        });
  }

  template <typename T>
  bool publishNumber(const char* topic, T value, bool retained = false, uint8_t precision = 2) {
    static_assert(std::is_arithmetic<T>::value, "T має бути числовим типом");
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
    static_assert(std::is_arithmetic<T>::value, "T має бути числовим типом");
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

  bool isConnected() const;

private:
  bool connect();
  void resubscribeAll();
  void dispatchMessage(const char* topic, const uint8_t* payload, unsigned int length);
  void cleanupRemovedListeners();
  void enqueueIncoming(const char* topic, const uint8_t* payload, unsigned int length);

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
  PicoMQTT::Client _mqttClient;
#endif

  std::vector<MqttListenerEntry> _listeners;
  MqttListenerId _nextListenerId = 1;
  uint32_t _lastReconnectAttempt = 0;

#if __has_include(<PubSubClient.h>)
  static MqttClient* _instance;
#endif

#if defined(ESP32)
  // --- Потокобезпека для мережевого таска (тільки ESP32) ---
  // _networkMutex захищає _mqttClient (весь мережевий I/O: connect/loop/publish/
  // subscribe) - до нього звертаються і мережевий таск, і головний потік
  // (publish()/subscribe() викликані з main.cpp).
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
  volatile bool _taskShouldRun = false;

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
