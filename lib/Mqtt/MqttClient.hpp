#pragma once

#include <Arduino.h>
#include <Client.h>
#include <PubSubClient.h>
#include <functional>
#include <vector>

/**
 * MqttClient
 *
 * Generic, protocol-agnostic MQTT client wrapper over PubSubClient.
 *
 * The transport is injected via the Arduino `Client&` interface, so the
 * caller decides whether the connection is plain TCP (WiFiClient, port 1883)
 * or TLS (WiFiClientSecure, port 8883) — this class does not know or care.
 *
 * Non-blocking: `update()` must be called frequently (e.g. every main loop
 * iteration, or from a TaskController CronTask). Reconnection attempts are
 * throttled internally via `_reconnectIntervalMs` and never block execution.
 *
 * NOTE: PubSubClient's message callback is a plain C function pointer with
 * no user-context parameter, so only one MqttClient instance can be active
 * at a time in the firmware (a single static instance pointer is used as a
 * trampoline). This matches current project usage (single broker/session).
 */
class MqttClient {
public:
    using MessageCallback = std::function<void(const String& topic, const String& payload)>;

    MqttClient(Client& netClient, const char* host, uint16_t port, const char* clientId);

    // Optional username/password authentication (works for both plain and TLS).
    void setCredentials(const char* username, const char* password);

    void setReconnectIntervalMs(uint32_t intervalMs) { _reconnectIntervalMs = intervalMs; }

    // Call once, after WiFi is connected.
    void begin();

    // Call frequently from the main loop / scheduler. Handles PubSubClient::loop()
    // and non-blocking reconnection.
    void update();

    bool isConnected();

    bool publish(const char* topic, const String& payload, bool retain = false);

    // Registers a callback for a topic. Re-applied automatically after reconnects.
    void subscribe(const char* topic, MessageCallback callback);

private:
    struct Subscription {
        String topic;
        MessageCallback callback;
    };

    void _tryReconnect();
    void _resubscribeAll();
    void _onMessage(char* topic, uint8_t* payload, unsigned int length);

    PubSubClient _client;
    String _host;
    uint16_t _port;
    String _clientId;
    String _username;
    String _password;
    bool _hasCredentials = false;

    std::vector<Subscription> _subscriptions;

    uint32_t _reconnectIntervalMs = 5000;
    unsigned long _lastReconnectAttemptMs = 0;

    static MqttClient* _instanceForCallback;
};
