#pragma once

#include <cstdint>

// Конфігурація підключення до MQTT-брокера.
// Підтримує: plain / TLS, з CA-сертифікатом або без (insecure), auth опційний.
struct MqttConfig {
    const char* host = nullptr;
    uint16_t port = 1883;
    const char* clientId = "esp32-client";

    // TLS
    bool useTls = false;
    // PEM CA-сертифікат (null-terminated). Якщо useTls == true і caCert == nullptr -> setInsecure().
    const char* caCert = nullptr;

    // Автентифікація (plain user/password, працює як з TLS, так і без)
    bool useAuth = false;
    const char* username = nullptr;
    const char* password = nullptr;

    uint16_t bufferSize = 512;
    uint32_t reconnectIntervalMs = 5000;
};
