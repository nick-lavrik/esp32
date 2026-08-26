#pragma once

#include <Arduino.h>

// Один пристрій з відповіді GET /iot-open/sign/device/list.
struct EcoflowDevice {
    String serialNumber;  // "sn" - ключовий сегмент усіх MQTT-топіків пристрою
    String name;          // "deviceName" (може бути порожнім, якщо не задано в застосунку)
    bool online = false;  // "online": 1/0
};
