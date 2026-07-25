#pragma once
#include <cstdint>

// Версія 1 — початковий формат (для демонстрації міграції)
struct SensorConfigV1 {
    float threshold;
    uint32_t intervalMs;
};

// Версія 2 — додано нове поле alarmEnabled
struct SensorConfigV2 {
    float threshold;
    uint32_t intervalMs;
    bool alarmEnabled;
};

// Поточна (актуальна) версія структури, яку використовує прошивка
using SensorConfig = SensorConfigV2;

constexpr uint32_t SENSOR_CONFIG_MAGIC = 0x53454E53;   // 'SENS'
constexpr uint16_t SENSOR_CONFIG_VERSION = 2;
