#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <MqttClient.hpp>
#include <functional>
#include <string>
#include <vector>

#include "EcoFlowAuthClient.hpp"
#include "EcoFlowDevice.hpp"

// Підключення до хмарного MQTT-брокера EcoFlow Open Platform.
//
// Складається з двох незалежних частин:
//   * REST (EcoFlowAuthClient, accessKey/secretKey) - список пристроїв і, за
//     потреби, перевипуск MQTT-креденшелів. Виклики БЛОКУЮЧІ (HTTPS ~1-2 с),
//     тому робляться лише з головного потоку і на вимогу, не в циклі.
//   * MQTT (MqttClient поверх PicoMQTT + TLS) - постійна підписка на
//     телеметрію. Живе у власному FreeRTOS-таску, як і основний MqttClient.
//
// MQTT-креденшели беруться з secrets.ini (ECOFLOW_MQTT_*) - це збережена
// відповідь GET /iot-open/sign/certification. Вони довгоживучі, тому
// REST-виклик на кожному старті не потрібен; refreshCredentials() є на випадок,
// якщо їх відкличуть.
class EcoFlowClient {
public:
    struct Config {
        const char *mqttHost = nullptr;
        uint16_t mqttPort = 8883;
        // certificateAccount - водночас MQTT username і {account} у схемі топіків.
        const char *mqttUsername = nullptr;
        const char *mqttPassword = nullptr;
        // Для REST. Можуть бути nullptr - тоді доступний лише MQTT.
        const char *accessKey = nullptr;
        const char *secretKey = nullptr;
        // Має бути унікальним у межах акаунта: два клієнти з однаковим id
        // брокер вибиває по черзі, утворюючи нескінченний reconnect-цикл.
        const char *clientId = "esp32-ecoflow";
    };

    using QuotaCallback = std::function<void(const String &serialNumber, JsonDocument &payload)>;
    using StatusCallback = std::function<void(const String &serialNumber, JsonDocument &payload)>;

    explicit EcoFlowClient(const Config &config);

    // Піднімає MQTT-підключення (асинхронно, у власному таску) і підписки.
    // ВАЖЛИВО: спершу робить блокуючий REST-запит за списком пристроїв, тому
    // потребує WiFi І СИНХРОНІЗОВАНОГО ЧАСУ (підпис EcoFlow містить timestamp;
    // з часом "з коробки" сервер відхиляє запит). Повторний виклик після
    // вдалого старту - no-op.
    void begin();

    // Те саме, але у власному таску: не блокує sketch loop() на час REST і не
    // робить TLS-хендшейк на стеку головного loopTask. Саме це викликає
    // відкладений старт із main.cpp, коли NTP нарешті синхронізувався.
    bool beginAsync();

    // false, доки список пристроїв не отримано і MQTT не піднято (напр. REST
    // провалився через мережу) - тоді старт варто повторити.
    bool isStarted() const { return _started; }
    bool isBusy() const { return _restBusy; }

    // Викликати з головного loop() - розбирає чергу вхідних повідомлень і
    // виконує колбеки в головному потоці.
    void loop();

    bool isConnected() const { return _mqtt.isConnected(); }

    // Скільки повідомлень прийшло з брокера і який топік був останнім.
    // Потрібно, щоб відрізнити "підключились, але пристрій мовчить" від
    // "дані йдуть, просто вимкнено verbose".
    uint32_t messageCount() const { return _messageCount; }
    const String &lastTopic() const { return _lastTopic; }

    // БЛОКУЮЧИЙ REST-виклик (потребує WiFi + синхронізованого часу для підпису).
    // Якщо MQTT уже піднятий, його TLS-сесія на час запиту рветься і
    // піднімається знову - див. withMqttSuspended().
    bool refreshDevices();
    const std::vector<EcoFlowDevice> &devices() const { return _devices; }

    // Ті самі REST-виклики, але у ВЛАСНОМУ таску: не блокують sketch loop() і,
    // головне, не роблять TLS-хендшейк на стеку головного loopTask (8 КБ) -
    // mbedTLS там на межі, а виклик з колбека serial-команди додає вкладеності.
    // Результат друкується в лог. Повертають false, якщо запит уже виконується.
    bool refreshDevicesAsync();
    bool refreshCredentialsAsync();

    // БЛОКУЮЧИЙ REST-виклик: перевипуск MQTT-креденшелів. Потрібен, лише якщо
    // ті, що в secrets.ini, перестали працювати - результат треба перенести в
    // secrets.ini вручну (у рантаймі перепідключення не робимо).
    bool refreshCredentials(EcoFlowMqttCredentials &outCredentials);

    // Телеметрія будь-якого пристрою акаунта.
    void onQuota(QuotaCallback callback) { _quotaCallback = callback; }
    // Онлайн/офлайн пристрою.
    void onStatus(StatusCallback callback) { _statusCallback = callback; }

    const String &account() const { return _account; }
    const String &lastError() const { return _lastError; }

private:
    Config _config;
    String _account;
    String _lastError;

    // ПОРЯДОК ЧЛЕНІВ ЗНАЧУЩИЙ: MqttConfig копіює лише вказівники на рядки, а
    // root-топік будується в рантаймі з account - тому сховище має бути
    // ініціалізоване (і жити) перед _mqtt та довше за нього.
    std::string _rootTopicStorage;

    MqttClient _mqtt;
    EcoFlowAuthClient _auth;

    std::vector<EcoFlowDevice> _devices;
    uint32_t _messageCount = 0;
    String _lastTopic;

    QuotaCallback _quotaCallback;
    StatusCallback _statusCallback;

    // Статичні (а не методи) - викликаються зі списку ініціалізації, коли
    // об'єкт ще не сконструйований; так виключаємо читання неініціалізованих
    // членів.
    static std::string buildRootTopic(const char *account);
    static MqttConfig makeMqttConfig(const Config &config, const std::string &rootTopic);

    // Виконує блокуючий REST-виклик, звільнивши перед цим TLS-сесію MQTT.
    // ESP32-C6 не тягне дві одночасні TLS-сесії: mbedTLS падає з
    // "BIGNUM - Memory allocation failed" ще на RSA-операції хендшейку
    // (кожна сесія - ~32 КБ буферів, CONFIG_MBEDTLS_SSL_MAX_CONTENT_LEN=16384).
    bool withMqttSuspended(const char *what, const std::function<bool()> &action);

    // true після begin() - до цього паузити нічого (і не можна: клієнт ще не
    // конфігурований).
    bool _started = false;

    // Один REST-таск за раз: паралельні запити змагались би за suspend().
    volatile bool _restBusy = false;

    enum class RestJob { kDevices, kCredentials, kStart };
    bool startRestTask(RestJob job);
    static void restTaskTrampoline(void *param);
    void runRestJob(RestJob job);

    // Витягує {sn} з "/open/{account}/{sn}/quota". Порожній рядок, якщо топік
    // не відповідає схемі.
    static String serialFromTopic(const char *topic);
};
