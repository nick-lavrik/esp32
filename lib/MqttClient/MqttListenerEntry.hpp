#pragma once

#include <cstdint>
#include <functional>
#include <Arduino.h>

using MqttListenerId = uint32_t;

// Повертає true -> продовжити обробку іншими лістенерами, false -> зупинити.
using MqttListenerCallback = std::function<bool(const char* topic, const uint8_t* payload, unsigned int length)>;

struct MqttListenerEntry {
    MqttListenerId id = 0;
    String topic;
    MqttListenerCallback callback;
    bool markedForRemoval = false;
};
