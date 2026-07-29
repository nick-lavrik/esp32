#include "MqttClient.hpp"

MqttClient* MqttClient::_instanceForCallback = nullptr;

MqttClient::MqttClient(Client& netClient, const char* host, uint16_t port, const char* clientId)
    : _client(netClient), _host(host), _port(port), _clientId(clientId) {
}

void MqttClient::setCredentials(const char* username, const char* password) {
    _username = username;
    _password = password;
    _hasCredentials = true;
}

void MqttClient::begin() {
    _client.setServer(_host.c_str(), _port);
    _client.setCallback([](char* topic, uint8_t* payload, unsigned int length) {
        if (MqttClient::_instanceForCallback != nullptr) {
            MqttClient::_instanceForCallback->_onMessage(topic, payload, length);
        }
    });
    MqttClient::_instanceForCallback = this;

    _tryReconnect();
}

void MqttClient::update() {
    if (!_client.connected()) {
        _tryReconnect();
        return;
    }
    _client.loop();
}

bool MqttClient::isConnected() {
    return _client.connected();
}

bool MqttClient::publish(const char* topic, const String& payload, bool retain) {
    if (!_client.connected()) {
        return false;
    }
    return _client.publish(topic, payload.c_str(), retain);
}

void MqttClient::subscribe(const char* topic, MessageCallback callback) {
    _subscriptions.push_back(Subscription{String(topic), callback});
    if (_client.connected()) {
        _client.subscribe(topic);
    }
}

void MqttClient::_tryReconnect() {
    unsigned long now = millis();
    if (_lastReconnectAttemptMs != 0 && (now - _lastReconnectAttemptMs) < _reconnectIntervalMs) {
        return;
    }
    _lastReconnectAttemptMs = now;

    bool connected = _hasCredentials
        ? _client.connect(_clientId.c_str(), _username.c_str(), _password.c_str())
        : _client.connect(_clientId.c_str());

    if (connected) {
        _resubscribeAll();
    }
}

void MqttClient::_resubscribeAll() {
    for (const auto& sub : _subscriptions) {
        _client.subscribe(sub.topic.c_str());
    }
}

void MqttClient::_onMessage(char* topic, uint8_t* payload, unsigned int length) {
    String topicStr(topic);

    String payloadStr;
    payloadStr.reserve(length);
    for (unsigned int i = 0; i < length; ++i) {
        payloadStr += static_cast<char>(payload[i]);
    }

    for (const auto& sub : _subscriptions) {
        if (sub.topic == topicStr && sub.callback) {
            sub.callback(topicStr, payloadStr);
        }
    }
}
