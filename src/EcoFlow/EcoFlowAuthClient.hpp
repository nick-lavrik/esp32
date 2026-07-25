#pragma once

#include <Arduino.h>
#include "EcoFlowMqttCredentials.hpp"

// Виконує автентифікацію в EcoFlow Open Platform (developer-eu.ecoflow.com)
// через accessKey/secretKey і отримує параметри підключення до MQTT-брокера.
class EcoFlowAuthClient {
public:
    EcoFlowAuthClient(const String &accessKey, const String &secretKey);

    // Виконує GET /iot-open/sign/certification і заповнює outCredentials.
    // Повертає true у разі успіху, false — у разі помилки мережі/API (див. lastError()).
    bool fetchMqttCredentials(EcoFlowMqttCredentials &outCredentials);

    const String &lastError() const { return _lastError; }

private:
    String _accessKey;
    String _secretKey;
    String _lastError;

    static const char *kApiHost;   // "api-e.ecoflow.com"
    static const char *kCertPath;  // "/iot-open/sign/certification"

    String generateNonce() const;
    String generateTimestamp() const;
};
