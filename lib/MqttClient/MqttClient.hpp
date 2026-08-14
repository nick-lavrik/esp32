#pragma once

#if __has_include(<PubSubClient.>)
#define HAS_MQTT_CLIENT 1
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>

#include <cstring>
#include <string>
#include <type_traits>
#include <vector>

#include "MqttConfig.hpp"
#include "MqttKeyGenerator.hpp"
#include "MqttListenerEntry.hpp"

// Спільний MQTT-клієнт (plain / TLS, з auth або без), з підпискою через лістенери.
// Topic filter підтримує wildcard '+' та '#' (див. MqttTopicMatcher).
//
// ОБМЕЖЕННЯ: PubSubClient використовує C-style callback (function pointer),
// тому активним може бути лише ОДИН екземпляр MqttClient одночасно
// (static-трамплін на _instance).
//
// Topic-префікс (напр. dev/prod/qa/local, регіон, тощо): застосовується автоматично до ВСІХ
// топіків (publish/subscribe/unsubscribe/addListener і похідні, LWT). Джерело - або
// MqttConfig::prefix (build-time дефолт), або зовнішній MqttKeyGenerator через
// setKeyGenerator() (напр. ConfigStorage-override), викликаний ДО begin(). Топік у
// callback листенера - це вже фактичний (префіксований) topic з брокера.
//
//   MqttConfig config;
//   config.prefix = MQTT_TOPIC_PREFIX;    // build-time дефолт з secrets.ini
//   MqttClient mqtt(config);
//   ...
//   String stored = configStorage.getString("mqtt-prefix", "");
//   static MqttKeyGenerator override;
//   if (stored.length() > 0) {            // runtime override - лише якщо реально є в сторедж
//     override.setPrefix(stored.c_str());
//     mqtt.setKeyGenerator(&override);    // ДО begin()
//   }
//   mqtt.begin();
class MqttClient {
public:
  explicit MqttClient(const MqttConfig& config);

  // Pointer injection (ESP32 style): виклик ДО begin() (точніше - до першого
  // addListener()/publish()/subscribe()/connect()) - інакше вже додані listeners лишаться
  // зі старим (baked-in) топіком, див. addListener().
  // Якщо не викликати взагалі - використовується внутрішній генератор з _config.prefix
  // (build-time дефолт з secrets.ini).
  void setKeyGenerator(MqttKeyGenerator* keyGenerator);

  // Активний генератор (injected через setKeyGenerator(), або _defaultKeyGenerator з
  // _config.prefix, якщо нічого не injected). Валідний завжди, навіть до begin().
  const MqttKeyGenerator& keyGenerator() const;

  void begin();
  void loop();
  // Graceful disconnect. LWT НЕ спрацьовує при штатному DISCONNECT (за MQTT-специфікацією),
  // тому offline-повідомлення публікується вручну перед відключенням (якщо lwtTopic задано).
  // customOfflineMessage - опційний override lwtOfflineMessage (наприклад для розрізнення
  // причини відключення: "offline (ota)" / "offline (planned reboot)" тощо).
  void disconnect(const char* customOfflineMessage = nullptr);

  bool publish(const char* topic, const char* payload, bool retained = false);
  bool publish(const char* topic, const uint8_t* payload, unsigned int length,
               bool retained = false);
  bool subscribe(const char* topic);
  bool unsubscribe(const char* topic);

  // topic (filter) може містити '+' / '#'. Повертає handle для removeListener().
  MqttListenerId addListener(const char* topic, MqttListenerCallback callback);

  // Payload як null-terminated const char* (рядковий, не бінарний, без sizeof/struct).
  MqttListenerId addStringListener(const char* topic, MqttStringListenerCallback callback);

  void removeListener(MqttListenerId id);

  // POD/struct (bin, не JSON). T має бути trivially copyable (без String/pointers/vector
  // всередині).
  //
  // Еволюція структур з часом (не всі reader'и/sender'и оновлюються одночасно):
  //  1. Нові поля додавай ЛИШЕ в кінець struct, ніколи не вставляй усередину і не видаляй існуючі.
  //  2. Обов'язково `__attribute__((packed))` - інакше compiler padding може відрізнятись між
  //  файлами.
  //  3. addStructListener() толерантний до довжини payload: коротший payload (старий sender) ->
  //     нові поля лишаються 0 (тому дефолтне значення нового поля має бути 0/false/nullptr-safe);
  //     довший payload (новий sender) -> зайві байти просто ігноруються.
  //  4. Рекомендовано перше поле - `uint16_t structVersion` для явного трекання версії
  //     (корисно для діагностики/логів, навіть якщо саме розширення покриває п.3).
  template <typename T>
  bool publishStruct(const char* topic, const T& value, bool retained = false) {
    static_assert(std::is_trivially_copyable<T>::value, "T має бути POD (trivially copyable)");
    return publish(topic, reinterpret_cast<const uint8_t*>(&value), sizeof(T), retained);
  }

  template <typename T>
  MqttListenerId addStructListener(const char* topic,
                                   std::function<void(const char*, const T&)> callback) {
    static_assert(std::is_trivially_copyable<T>::value, "T має бути POD (trivially copyable)");
    return addListener(
        topic, [callback](const char* topic, const uint8_t* payload, unsigned int length) -> void {
          T value{};
          unsigned int copyLength = length < sizeof(T) ? length : sizeof(T);
          memcpy(&value, payload, copyLength);
          callback(topic, value);
        });
  }

  // mqtt->publishNumber("sensors/temperature", 23.456f, false, 1); // "23.5"
  // mqtt->publishNumber("sensors/count", 42);                      // "42" (int)
  // mqtt->publishNumber("sensors/uptime", (uint32_t)millis());     // "128340"

  // mqtt->addNumberListener<float>("sensors/temperature", [](const char* topic, float value) ->
  // void {
  //     Serial.printf("temp=%.1f\n", value);
  // });

  // mqtt->addNumberListener<int32_t>("sensors/count", [](const char* topic, int32_t value) {
  //     Serial.printf("count=%d\n", value);
  // });

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
  MqttListenerId addNumberListener(const char* topic,
                                   std::function<void(const char*, T)> callback) {
    static_assert(std::is_arithmetic<T>::value, "T має бути числовим типом");
    return addStringListener(topic, [callback](const char* topic, const char* payload) -> void {
      T value;
      if constexpr (std::is_floating_point<T>::value) {
        value = static_cast<T>(strtod(payload, nullptr));
      } else if constexpr (std::is_signed<T>::value) {
        value = static_cast<T>(strtoll(payload, nullptr, 10));
      } else {
        value = static_cast<T>(strtoull(payload, nullptr, 10));
      }
      callback(topic, value);
    });
  }

  // JSON (ArduinoJson v7). Некоректний JSON у addJsonListener -> callback не викликається,
  bool publishJson(const char* topic, JsonDocument& doc, bool retained = false);
  MqttListenerId addJsonListener(const char* topic,
                                 std::function<void(const char*, JsonDocument&)> callback);

  bool isConnected() const;

private:
  bool connect();
  void resubscribeAll();
  void dispatchMessage(const char* topic, const uint8_t* payload, unsigned int length);
  void cleanupRemovedListeners();

  void handleMessage(char* topic, uint8_t* payload, unsigned int length);
  static void staticCallback(char* topic, uint8_t* payload, unsigned int length);

  // topic == nullptr -> "" (порожній std::string). Застосовує _keyGenerator, якщо injected.
  std::string resolveTopic(const char* topic) const;

  MqttConfig _config;
  MqttKeyGenerator _defaultKeyGenerator;  // з _config.prefix; fallback, якщо нічого не injected
  MqttKeyGenerator* _keyGenerator = nullptr;
  WiFiClient _plainClient;
  WiFiClientSecure _secureClient;
  PubSubClient _mqttClient;

  std::vector<MqttListenerEntry> _listeners;
  MqttListenerId _nextListenerId = 1;

  uint32_t _lastReconnectAttempt = 0;

  static MqttClient* _instance;
};

#else
#define HAS_MQTT_CLIENT 0
#endif
