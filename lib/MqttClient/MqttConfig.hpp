#pragma once

#include <cstdint>

// Конфігурація підключення до MQTT-брокера.
// Підтримує: plain / TLS, з CA-сертифікатом або без (insecure), auth опційний.
struct MqttConfig {
  const char* host = nullptr;
  uint16_t port = 1883;
  const char* clientId = "esp32-client";

  // TLS
  bool useTls = false;
  // PEM CA-сертифікат (null-terminated). Якщо useTls == true і caCert == nullptr -> setInsecure().
  const char* caCert = nullptr;

  // Автентифікація (plain user/password, працює як з TLS, так і без)
  bool useAuth = false;
  const char* username = nullptr;
  const char* password = nullptr;

  uint16_t bufferSize = 512;
  uint32_t reconnectIntervalMs = 5000;

  // Root-підписка PicoMQTT-гілки: MqttClient підписується на ОДИН цей топік і
  // сам роздає повідомлення по addListener()-фільтрах (MqttTopicMatcher).
  // "#" годиться для власного брокера, але сторонні (напр. EcoFlow Open
  // Platform) відхиляють підписку на '#' за ACL - там треба вказати
  // конкретний дозволений патерн, напр. "/open/{certificateAccount}/#".
  const char* rootSubscribeTopic = "#";

  // Максимальний розмір повідомлення, яке PicoMQTT збирає в пам'яті перед
  // передачею в колбек root-підписки. Довші повідомлення обрізаються, тому
  // для брокерів із "товстою" телеметрією (EcoFlow quota) 2 КБ замало.
  size_t rootSubscribeBufferSize = 2 * 1024;

  // Розмір стеку мережевого FreeRTOS-таска. 8 КБ вистачає для plain-TCP, але
  // mbedTLS-хендшейк (useTls) з'їдає помітно більше - для TLS ставимо 16 КБ.
  uint32_t taskStackSize = 8192;

  // Ім'я мережевого таска (видно в "dump-tasks"/vTaskList). Для другого
  // екземпляра MqttClient варто задати своє, щоб таски розрізнялись.
  const char* taskName = "mqtt-net";

  // false -> topic передається брокеру ЯК Є, повз MqttKeyGenerator.
  // Потрібно для брокерів із жорстко заданою схемою топіків: EcoFlow вимагає
  // ПРОВІДНИЙ слеш ("/open/..."), а MqttKeyGenerator::trimSlashes() його
  // зрізає (див. MqttKeyGenerator.cpp).
  bool useKeyGenerator = true;

  // LWT (опціонально). Якщо lwtTopic або lwtOfflineMessage nullptr/порожні - LWT ігнорується.
  // lwtOnlineMessage (опціонально) - публікується автоматично в lwtTopic одразу після
  // кожного вдалого connect() (незалежно від lwtOfflineMessage), якщо lwtTopic задано.
  // PubSubClient підтримує лише QoS 0/1.
  const char* lwtTopic = nullptr;
  const char* lwtOfflineMessage = nullptr;
  const char* lwtOnlineMessage = nullptr;
  uint8_t lwtQos = 0;
  bool lwtRetain = false;

  // Topic-префікс за замовчуванням (напр. dev/prod/qa/local, регіон, тощо) для
  // MqttKeyGenerator. ОБОВ'ЯЗКОВО build-time літерал (макрос/секрет з secrets.ini), як
  // host/clientId - NEVER Arduino::String::c_str() (temporary -> dangling pointer).
  // Використовується автоматично, якщо MqttClient::setKeyGenerator() не викликали до
  // begin() з окремим (напр. ConfigStorage-based) генератором. nullptr/"" -> без префікса.
  const char* prefix = nullptr;
};
