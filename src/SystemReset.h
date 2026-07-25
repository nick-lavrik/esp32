#pragma once

#include <Arduino.h>
#include "esp_system.h"
#include "esp_task_wdt.h"

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
    // і треба гарантовано перезавантажити його навіть без штатного шляху
    static inline void rebootViaWatchdog(uint32_t timeoutSeconds = 1) {
        Serial.println("[SystemReset] Rebooting via watchdog...");
        Serial.flush();
        esp_task_wdt_init(timeoutSeconds, true); // panic=true -> reset при спрацюванні
        esp_task_wdt_add(NULL);
        while (true) {
            // навмисно нічого не робимо, чекаємо на watchdog
        }
    }

    // Повертає причину останнього ресету у вигляді рядка
    static inline const char* getLastResetReason() {
        esp_reset_reason_t reason = esp_reset_reason();
        switch (reason) {
            case ESP_RST_POWERON:  return "Power-on reset";
            case ESP_RST_EXT:      return "External pin reset";
            case ESP_RST_SW:       return "Software reset (ESP.restart)";
            case ESP_RST_PANIC:    return "Reset due to panic";
            case ESP_RST_INT_WDT:  return "Interrupt watchdog reset";
            case ESP_RST_TASK_WDT: return "Task watchdog reset";
            case ESP_RST_WDT:      return "Other watchdog reset";
            case ESP_RST_DEEPSLEEP:return "Wake from deep sleep";
            case ESP_RST_BROWNOUT: return "Brownout reset";
            case ESP_RST_SDIO:     return "SDIO reset";
            default:                return "Unknown reset reason";
        }
    }

    // Зручний метод для логування причини ресету при старті
    static inline void printLastResetReason() {
        Serial.printf("[SystemReset] Last reset reason: %s\n", getLastResetReason());
    }
};
