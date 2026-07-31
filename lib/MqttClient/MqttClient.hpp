#pragma once

#include <vector>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include "MqttConfig.hpp"
#include "MqttListenerEntry.hpp"

// Спільний MQTT-клієнт (plain / TLS, з auth або без), з підпискою через лістенери.
// Topic filter підтримує wildcard '+' та '#' (див. MqttTopicMatcher).
//
// ОБМЕЖЕННЯ: PubSubClient використовує C-style callback (function pointer),
// тому активним може бути лише ОДИН екземпляр MqttClient одночасно
// (static-трамплін на _instance).
class MqttClient {
public:
    explicit MqttClient(const MqttConfig& config);

    void begin();
    void loop();

    bool publish(const char* topic, const char* payload, bool retained = false);
    bool subscribe(const char* topic);
    bool unsubscribe(const char* topic);

    // topic (filter) може містити '+' / '#'. Повертає handle для removeListener().
    MqttListenerId addListener(const char* topic, MqttListenerCallback callback);
    void removeListener(MqttListenerId id);

    bool isConnected() const;

private:
    bool connect();
    void resubscribeAll();
    void dispatchMessage(const char* topic, const uint8_t* payload, unsigned int length);
    void cleanupRemovedListeners();

    void handleMessage(char* topic, uint8_t* payload, unsigned int length);
    static void staticCallback(char* topic, uint8_t* payload, unsigned int length);

    MqttConfig _config;
    WiFiClient _plainClient;
    WiFiClientSecure _secureClient;
    PubSubClient _mqttClient;

    std::vector<MqttListenerEntry> _listeners;
    MqttListenerId _nextListenerId = 1;

    uint32_t _lastReconnectAttempt = 0;

    static MqttClient* _instance;
};
