#pragma once

#include <cstdint>
#include <functional>
#include <Arduino.h>

using MqttListenerId = uint32_t;

using MqttListenerCallback = std::function<void(const char* topic, const uint8_t* payload, unsigned int length)>;

// Payload як null-terminated рядок (зручно для текстових/JSON-топіків без бінарних даних).
using MqttStringListenerCallback = std::function<void(const char* topic, const char* payload)>;

struct MqttListenerEntry {
    MqttListenerId id = 0;
    String topic;
    MqttListenerCallback callback;
    bool markedForRemoval = false;
};
