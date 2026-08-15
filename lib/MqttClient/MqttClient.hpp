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

#include <cstring>
#include <string>
#include <type_traits>
#include <vector>

#include "MqttConfig.hpp"
#include "MqttKeyGenerator.hpp"
#include "MqttListenerEntry.hpp"

class MqttClient {
public:
  explicit MqttClient(const MqttConfig& config);

  void setKeyGenerator(MqttKeyGenerator* keyGenerator);
  const MqttKeyGenerator& keyGenerator() const;

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

  std::string resolveTopic(const char* topic) const;

  MqttConfig _config;
  MqttKeyGenerator _defaultKeyGenerator;
  MqttKeyGenerator* _keyGenerator = nullptr;

#if __has_include(<PubSubClient.h>)
  WiFiClient _plainClient;
  WiFiClientSecure _secureClient;
  PubSubClient _mqttClient;
  
  void handleMessage(char* topic, uint8_t* payload, unsigned int length);
  static void staticCallback(char* topic, uint8_t* payload, unsigned int length);
#elif __has_include(<PicoMQTT.h>)
  PicoMQTT::Client _mqttClient;
#endif

  std::vector<MqttListenerEntry> _listeners;
  MqttListenerId _nextListenerId = 1;
  uint32_t _lastReconnectAttempt = 0;

#if __has_include(<PubSubClient.h>)
  static MqttClient* _instance;
#endif
};

#else
#define HAS_MQTT_CLIENT 0
#endif
