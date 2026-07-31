#include "MqttClient.hpp"
#include "MqttTopicMatcher.hpp"
#include <Arduino.h>
#include <algorithm>

MqttClient* MqttClient::_instance = nullptr;

MqttClient::MqttClient(const MqttConfig& config)
    : _config(config) {
}

void MqttClient::begin() {
    _instance = this;

    if (_config.useTls) {
        if (_config.caCert != nullptr) {
            _secureClient.setCACert(_config.caCert);
        } else {
            _secureClient.setInsecure();
        }
        _mqttClient.setClient(_secureClient);
    } else {
        _mqttClient.setClient(_plainClient);
    }

    _mqttClient.setServer(_config.host, _config.port);
    _mqttClient.setBufferSize(_config.bufferSize);
    _mqttClient.setCallback(MqttClient::staticCallback);
}

void MqttClient::loop() {
    if (!_mqttClient.connected()) {
        uint32_t now = millis();
        if (now - _lastReconnectAttempt >= _config.reconnectIntervalMs) {
            _lastReconnectAttempt = now;
            connect();
        }
        return;
    }

    _mqttClient.loop();
}

bool MqttClient::connect() {
    bool ok = _config.useAuth
        ? _mqttClient.connect(_config.clientId, _config.username, _config.password)
        : _mqttClient.connect(_config.clientId);

    if (ok) {
        resubscribeAll();
    }

    return ok;
}

bool MqttClient::publish(const char* topic, const char* payload, bool retained) {
    return _mqttClient.publish(topic, payload, retained);
}

bool MqttClient::subscribe(const char* topic) {
    return _mqttClient.subscribe(topic);
}

bool MqttClient::unsubscribe(const char* topic) {
    return _mqttClient.unsubscribe(topic);
}

MqttListenerId MqttClient::addListener(const char* topic, MqttListenerCallback callback) {
    MqttListenerEntry entry;
    entry.id = _nextListenerId++;
    entry.topic = topic;
    entry.callback = callback;
    _listeners.push_back(entry);

    if (_mqttClient.connected()) {
        _mqttClient.subscribe(topic);
    }

    return entry.id;
}

void MqttClient::removeListener(MqttListenerId id) {
    for (auto& entry : _listeners) {
        if (entry.id == id) {
            entry.markedForRemoval = true;
            return;
        }
    }
}

void MqttClient::cleanupRemovedListeners() {
    _listeners.erase(
        std::remove_if(_listeners.begin(), _listeners.end(),
            [](const MqttListenerEntry& entry) { return entry.markedForRemoval; }),
        _listeners.end());
}

void MqttClient::resubscribeAll() {
    for (const auto& entry : _listeners) {
        if (!entry.markedForRemoval) {
            _mqttClient.subscribe(entry.topic.c_str());
        }
    }
}

void MqttClient::dispatchMessage(const char* topic, const uint8_t* payload, unsigned int length) {
    for (auto& entry : _listeners) {
        if (entry.markedForRemoval) {
            continue;
        }

        if (!MqttTopicMatcher::match(entry.topic.c_str(), topic)) {
            continue;
        }

        bool shouldContinue = entry.callback ? entry.callback(topic, payload, length) : true;
        if (!shouldContinue) {
            break;
        }
    }

    cleanupRemovedListeners();
}

bool MqttClient::isConnected() const {
    return const_cast<PubSubClient&>(_mqttClient).connected();
}

void MqttClient::handleMessage(char* topic, uint8_t* payload, unsigned int length) {
    dispatchMessage(topic, payload, length);
}

void MqttClient::staticCallback(char* topic, uint8_t* payload, unsigned int length) {
    if (_instance != nullptr) {
        _instance->handleMessage(topic, payload, length);
    }
}
