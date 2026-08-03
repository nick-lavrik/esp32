#pragma once

#include <Arduino.h>
#if defined(ESP32)
#include "esp_system.h"
#include "esp_task_wdt.h"
#endif

// SystemReset — статичний клас-утиліта для програмного перезавантаження ESP32
// та визначення причини останнього ресету.
class SystemReset {
public:
    // Штатний програмний reset
    static inline void reboot() {
        Serial.println("[SystemReset] Rebooting...");
        Serial.flush();
        delay(100);
        ESP.restart();
    }

    // Reset через watchdog — використовувати, якщо пристрій може "зависнути"
    // і треба гарантовано перезавантажити його навіть без штатного шляху.
    // API відповідає esp_task_wdt_config_t (ESP-IDF v5+).
    static inline void rebootViaWatchdog(uint32_t timeoutMs = 1000) {
        #if defined(ESP32)
        Serial.println("[SystemReset] Rebooting via watchdog...");
        Serial.flush();

        esp_task_wdt_config_t config = {
            .timeout_ms = timeoutMs,
            .idle_core_mask = 0,     // не чіпаємо idle-задачі
            .trigger_panic = true    // panic -> гарантований reset
        };

        // Якщо watchdog вже ініціалізований раніше (наприклад, фреймворком),
        // esp_task_wdt_init поверне ESP_ERR_INVALID_STATE — це нормально,
        // достатньо додати поточну задачу.
        esp_err_t err = esp_task_wdt_init(&config);
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
            Serial.printf("[SystemReset] esp_task_wdt_init failed: %s\n", esp_err_to_name(err));
        }

        esp_task_wdt_add(NULL);

        while (true) {
            // навмисно нічого не робимо, чекаємо на спрацювання watchdog
        }
        #endif
    }

    // Повертає причину останнього ресету у вигляді рядка
    static const char* getLastResetReason() {
        #if ESP32
        esp_reset_reason_t reason = esp_reset_reason();
        switch (reason) {
            case ESP_RST_POWERON:   return "Power-on reset";
            case ESP_RST_EXT:       return "External pin reset";
            case ESP_RST_SW:        return "Software reset (ESP.restart)";
            case ESP_RST_PANIC:     return "Reset due to panic";
            case ESP_RST_INT_WDT:   return "Interrupt watchdog reset";
            case ESP_RST_TASK_WDT:  return "Task watchdog reset";
            case ESP_RST_WDT:       return "Other watchdog reset";
            case ESP_RST_DEEPSLEEP: return "Wake from deep sleep";
            case ESP_RST_BROWNOUT:  return "Brownout reset";
            case ESP_RST_SDIO:      return "SDIO reset";
            default:                return "Unknown reset reason";
        }
        #endif

        #if ESP8266
        static char buff[32];
        snprintf(buff, sizeof(buff), "%s", ESP.getResetReason().c_str());
        return buff;
        #endif

        return nullptr;
    }

    // Зручний метод для логування причини ресету при старті
    static inline void printLastResetReason() {
        Serial.printf("[SystemReset] Last reset reason: %s\n", getLastResetReason());
    }
};
