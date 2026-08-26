#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <vector>

#include "EcoflowDevice.hpp"
#include "EcoflowMqttCredentials.hpp"

// Виконує автентифікацію в EcoFlow Open Platform (developer-eu.ecoflow.com)
// через accessKey/secretKey і отримує параметри підключення до MQTT-брокера.
class EcoflowAuthClient {
public:
    EcoflowAuthClient(const String &accessKey, const String &secretKey);

    // Виконує GET /iot-open/sign/certification і заповнює outCredentials.
    // Повертає true у разі успіху, false — у разі помилки мережі/API (див. lastError()).
    bool fetchMqttCredentials(EcoflowMqttCredentials &outCredentials);

    // Виконує GET /iot-open/sign/device/list і заповнює outDevices (список
    // очищується перед заповненням). Потрібен, щоб дізнатись серійні номери:
    // без sn не побудувати жодного MQTT-топіка (див. EcoflowMqttTopics.hpp).
    bool fetchDeviceList(std::vector<EcoflowDevice> &outDevices);

    // GET /iot-open/sign/device/quota/all?sn=... - ПОВНИЙ знімок стану пристрою
    // (242-353 поля). Потрібен тому, що MQTT-quota приходить дельтами: після
    // старту частина полів (зокрема inv.acInVol, за яким визначається наявність
    // мережі) може не приходити годинами.
    //
    // Доступний не для всіх пристроїв: DELTA mini і Smart Generator віддають
    // 1006 "current device is not allowed to get device info". Тому окремо
    // повертається notAllowed - щоб не смикати такий пристрій повторно.
    bool fetchQuotaAll(const String &serialNumber, JsonDocument &outDoc, bool &notAllowed);

    const String &lastError() const { return _lastError; }

private:
    String _accessKey;
    String _secretKey;
    String _lastError;

    static const char *kApiHost;    // "api-e.ecoflow.com"
    static const char *kCertPath;   // "/iot-open/sign/certification"
    static const char *kDevicePath; // "/iot-open/sign/device/list"
    static const char *kQuotaAllPath; // "/iot-open/sign/device/quota/all"

    String generateNonce() const;
    String generateTimestamp() const;

    // Спільна частина обох запитів: підписаний GET без query-параметрів.
    // Повертає true і кладе тіло відповіді в outPayload; інакше false і
    // заповнює _lastError.
    // query - те, що йде після '?' (без нього). У ПІДПИС не входить: EcoFlow
    // рахує canonical string лише з accessKey/nonce/timestamp (див. EcoflowSigner).
    bool signedGet(const char *path, String &outPayload, const String &query = String());
};
