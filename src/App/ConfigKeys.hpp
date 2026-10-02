#pragma once

// Ключі ConfigStorage (NVS) застосунку - одне місце на всі TU.
//
//   #include "App/ConfigKeys.hpp"
//   String stored = configStorage.getString(CFG_WATCHDOG, "");
//
// Ключ NVS - максимум 15 символів (ConfigStorage::MAX_KEY_LENGTH).
// inline constexpr, а не "const char* X = ...": заголовок включають кілька
// TU, і звичайне означення дало б duplicate symbol.

inline constexpr const char* CFG_SHOW_CLOCK = "clock";
// Зберігається лише РЕКОРД: сам режим гри після ресету не відновлюється
// (див. коментар у loadConfig(), src/main.cpp).
inline constexpr const char* CFG_DINO_HIGHSCORE = "dino.hi";
inline constexpr const char* CFG_BLINK_LED = "blink";  // ESP8266 BLINK_LED_PIN dependency
inline constexpr const char* CFG_SYS_AUTOBRIGHTNESS = "auto-brightness";
inline constexpr const char* CFG_DISPLAY_BRIGHTNESS = "brightness";
// runtime override для MQTT_TOPIC_PREFIX (напр. dev/prod/qa/local, регіон, тощо); порожнє
// -> дефолт з secrets.ini
inline constexpr const char* CFG_MQTT_TOPIC_PREFIX = "mqtt.prefix";
// runtime-override для ECOFLOW_AUTOCONNECT: "1"/"0"; порожнє -> build-time дефолт
inline constexpr const char* CFG_ECOFLOW_AUTOCONNECT = "ecoflow.auto";
// runtime-override: чи тягнути REST-знімок ('ecoflow-sync all') ПЕРЕД MQTT-
// конектом на старті (docs/tech_debt.md, розрив REST/MQTT); перемикається
// тією ж командою - 'ecoflow-sync on|off'. "1"/"0"; порожнє -> build-time
// дефолт. Не "ecoflow.sboot" - читається як "secure boot".
inline constexpr const char* CFG_ECOFLOW_SYNC_BOOT = "ecoflow.sync";
// dump ecoflow device status each minute
inline constexpr const char* CFG_ECOFLOW_WATCH = "ecoflow.watch";
// runtime-override: чи писати журнал переходів grid у NVS (команда
// 'ecoflow-journal on|off'); "1"/"0", порожнє -> увімкнено. Вимикає лише
// NVS-частину EcoflowGridJournal - MQTT-дзеркало не залежить.
inline constexpr const char* CFG_ECOFLOW_JOURNAL = "ecoflow.jrnl";

// Build-time дефолт для Watchdog::begin() (Watchdog.hpp, озброюється в кінці
// setup()). Тут, поруч із ключем runtime-override, бо на нього дивляться і
// setup(), і команда 'watchdog'.
#ifndef WATCHDOG_ENABLED
#define WATCHDOG_ENABLED 1
#endif
// runtime-override для WATCHDOG_ENABLED: "1"/"0"; порожнє -> build-time
// дефолт. Вимикати перед довгими SD-командами (sdbench/sdcrc/sdmap - свідомо
// блокують loop() на десятки секунд, CommandQueue.cpp) - інакше watchdog
// зніме плату посеред легітимного вимірювання. Команда 'watchdog on|off'.
inline constexpr const char* CFG_WATCHDOG = "watchdog";
// periodic 'heap' sampling (тимчасова діагностика фрагментації, docs/tech_debt.md)
inline constexpr const char* CFG_HEAP_WATCH = "heap.watch";
// Останні випущені app-креденшели (команда 'ecoflow-login'). Зберігаються
// як резервна копія й журнал: застосувати їх з NVS на льоту не можна - MqttConfig
// копіює вказівники в конструкторі глобального EcoflowClient, тобто до setup().
inline constexpr const char* CFG_ECOFLOW_APP_ACCOUNT = "ecoflow.acc";
inline constexpr const char* CFG_ECOFLOW_APP_PASSWORD = "ecoflow.pass";
inline constexpr const char* CFG_ECOFLOW_APP_USER_ID = "ecoflow.uid";
