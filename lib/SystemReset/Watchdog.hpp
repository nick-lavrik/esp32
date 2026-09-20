#pragma once

#include <Arduino.h>
#if defined(ESP32)
#include "esp_task_wdt.h"
#endif
#include <TLogger.hpp>

// Watchdog — постійний, фоновий сторож задачі loop(): якщо вона не встигає
// повернутись у keepalive() протягом timeoutMs - panic (гарантований reset
// із backtrace на serial, monitor_filters = esp32_exception_decoder у
// platformio.ini його одразу розшифрує) замість мовчазного зависання без
// жодного відновлення.
//
// На відміну від SystemReset::rebootViaWatchdog() (та сама бібліотека,
// свідомий разовий "повісь мене зараз") - цей працює постійно й ловить
// НЕПЕРЕДБАЧЕНЕ зависання. Різні задачі, тому окремий клас (CLAUDE.md,
// "Різні вимоги — різні механізми").
//
// Низькорівневі, довгі за задумом операції (sdbench/sdcrc/sdmap,
// lib/SDRawReader - свідомо блокують loop() на десятки секунд,
// CommandQueue.cpp) про Watchdog НЕ знають і не повинні: платформо-
// незалежна бібліотека читання SD не має залежати від watchdog застосунку.
// Для них - вимкнути Watchdog::begin() на боці виклику (команда 'watchdog
// off', застосовується з наступного ребуту, src/main.cpp).
class Watchdog {
public:
  // Викликати ОДИН РАЗ, наприкінці setup() (щоб самі кроки setupXxx() не
  // потрапляли під таймаут - деякі з них законно довші за секунду), той
  // самий контекст (задача), що потім виконує loop().
  static inline void begin(uint32_t timeoutMs = 60000) {
#if defined(ESP32)
    esp_task_wdt_config_t config = {
        .timeout_ms = timeoutMs,
        .idle_core_mask = 0,  // не чіпаємо idle-задачі
        .trigger_panic = true};

    // ESP_ERR_INVALID_STATE - watchdog вже ініціалізований фреймворком із
    // власним таймаутом; це нормально, додати задачу можна і так.
    esp_err_t err = esp_task_wdt_init(&config);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
      logger().error("esp_task_wdt_init failed: %s", esp_err_to_name(err));
    }
    esp_task_wdt_add(NULL);
#endif
  }

  // Викликати на КОЖНІЙ ітерації loop() (найперший рядок, до будь-яких
  // ранніх return - інакше саме та гілка, що їх мала, і "не годує" watchdog).
  static inline void keepalive() {
#if defined(ESP32)
    esp_task_wdt_reset();
#endif
  }

private:
  Watchdog() = delete;  // лише статичні методи

  static const TLogger& logger() {
    static const TLogger instance{"wdog"};
    return instance;
  }
};
