#pragma once

#include <Arduino.h>

// Дані для підключення до MQTT-брокера EcoFlow, отримані через
// EcoFlowAuthClient::fetchMqttCredentials().
struct EcoFlowMqttCredentials {
    String certificateAccount;   // використовується як MQTT username
    String certificatePassword;  // використовується як MQTT password
    String url;                  // напр. "mqtt-e.ecoflow.com"
    uint16_t port = 8883;
    String protocol;             // напр. "mqtts"

    bool isValid() const {
        return certificateAccount.length() > 0 &&
               certificatePassword.length() > 0 &&
               url.length() > 0;
    }
};
