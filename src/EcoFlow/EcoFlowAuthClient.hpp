#pragma once

#include <Arduino.h>
#include <vector>

#include "EcoFlowDevice.hpp"
#include "EcoFlowMqttCredentials.hpp"

// Виконує автентифікацію в EcoFlow Open Platform (developer-eu.ecoflow.com)
// через accessKey/secretKey і отримує параметри підключення до MQTT-брокера.
class EcoFlowAuthClient {
public:
    EcoFlowAuthClient(const String &accessKey, const String &secretKey);

    // Виконує GET /iot-open/sign/certification і заповнює outCredentials.
    // Повертає true у разі успіху, false — у разі помилки мережі/API (див. lastError()).
    bool fetchMqttCredentials(EcoFlowMqttCredentials &outCredentials);

    // Виконує GET /iot-open/sign/device/list і заповнює outDevices (список
    // очищується перед заповненням). Потрібен, щоб дізнатись серійні номери:
    // без sn не побудувати жодного MQTT-топіка (див. EcoFlowMqttTopics.hpp).
    bool fetchDeviceList(std::vector<EcoFlowDevice> &outDevices);

    const String &lastError() const { return _lastError; }

private:
    String _accessKey;
    String _secretKey;
    String _lastError;

    static const char *kApiHost;    // "api-e.ecoflow.com"
    static const char *kCertPath;   // "/iot-open/sign/certification"
    static const char *kDevicePath; // "/iot-open/sign/device/list"

    String generateNonce() const;
    String generateTimestamp() const;

    // Спільна частина обох запитів: підписаний GET без query-параметрів.
    // Повертає true і кладе тіло відповіді в outPayload; інакше false і
    // заповнює _lastError.
    bool signedGet(const char *path, String &outPayload);
};
