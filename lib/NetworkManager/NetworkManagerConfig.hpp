#pragma once
#include <stdint.h>
#include <string>

// Конфігурація NetworkManager.
//
// Зберігається в ConfigStorage під ключем "nm_config" як JSON.
//
// Використання:
//   NetworkManagerConfig cfg;
//   cfg.apSsid = "ESP32-AP";
//   cfg.apPassword = "12345678";
//   cfg.autoReconnect = true;
//   nm.setConfig(cfg);
//   nm.saveConfig();

struct NetworkManagerConfig {
  // ---- сканування ----
  uint32_t scanIntervalMs = 60000;  // інтервал між скануваннями в AP_MODE / RECONNECTING

  // ---- підключення ----
  uint32_t connectTimeoutMs = 10000;  // таймаут однієї спроби підключення
  uint32_t retryDelayMs = 2000;       // затримка між повторними спробами
  uint8_t maxRetries = 3;             // глобальний default (якщо WifiConnection::maxRetries == -1)
  bool scanBeforeConnect = true;      // true  → scan → filter by visible → connect
                                      // false → перебирати список без попереднього скану

  // ---- автоперепідключення ----
  bool autoReconnect = true;  // false → менеджер нічого не робить після втрати з'єднання
                              //         або після невдалих спроб; потрібен ручний виклик

  // ---- точка доступу (AP fallback) ----
  std::string apSsid = "ESP-NetworkManager";
  std::string apPassword = "";      // порожній рядок → відкрита мережа
  uint8_t apChannel = 1;
  std::string apIp = "192.168.4.1";
};
