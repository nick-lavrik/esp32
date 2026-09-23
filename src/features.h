#pragma once

// Єдиний каталог усіх BOARD_HAS_*/HAS_* прапорців, що будь-коли з'являються
// в "Можливості та фічі" (розділ 2) будь-якого src-<env>/environment.h
// (CLAUDE.md, "environment.h кожної плати"). Розділення по 8 environment.h
// загубило спільну картину "які фічі взагалі є в проєкті" - цей файл її
// повертає, без окремого Python-генератора.
//
// Свідомо НЕ включає прапорці, що самі себе визначають через __has_include
// у власних бібліотечних заголовках (HAS_MQTT_CLIENT - lib/MqttClient/
// MqttClient.hpp, HAS_CONSOLE_MQTT - lib/ConsoleMqtt/ConsoleMqtt.hpp,
// HAS_GMAIL_SENDER - lib/GmailSender/GmailSender.hpp, HAS_SCREEN_MIRROR -
// src/Display.h, HAS_PING_LIB - src/ping.h): для них "картина не губиться" -
// кожен визначається в ОДНОМУ місці, не в 8 environment.h, тож проблема,
// яку вирішує цей каталог, до них не застосовна. Показати їх тут теж було б
// хитким: макрос має бути визначений ДО цього заголовка, інакше мовчки "0"
// (undefined -> 0 - навмисно безпечно для #if нижче, але оманливо, якщо
// реальний стан платформи саме "не визначено ще").
//
// -include src-<env>/environment.h (platformio.ini) - глобальний build_flag,
// діє на ВЕСЬ TU з першого рядка, тому порядок #include цього файлу в
// main.cpp не має значення: усі BOARD_HAS_*/HAS_* нижче або вже мають
// реальне значення з environment.h конкретного env, або підставляються "0"
// нижче для env, які цей прапорець узагалі не визначають (наприклад
// BOARD_HAS_PSRAM - лише в env з PSRAM-чипом).

#ifndef BOARD_HAS_DISPLAY
#define BOARD_HAS_DISPLAY 0
#endif
#ifndef BOARD_HAS_TOUCHSCREEN
#define BOARD_HAS_TOUCHSCREEN 0
#endif
#ifndef BOARD_HAS_SD
#define BOARD_HAS_SD 0
#endif
#ifndef BOARD_HAS_IMU
#define BOARD_HAS_IMU 0
#endif
#ifndef BOARD_HAS_PSRAM
#define BOARD_HAS_PSRAM 0
#endif
#ifndef HAS_DINO_GAME
#define HAS_DINO_GAME 0
#endif
#ifndef HAS_ECOFLOW_CLIENT
#define HAS_ECOFLOW_CLIENT 0
#endif
#ifndef HAS_WEB_PORTAL
#define HAS_WEB_PORTAL 0
#endif

// HAS_LIGHT_SENSOR - похідний прапорець, не прямий BOARD_HAS_*/HAS_* з
// environment.h. Джерело - LIGHT_SENSOR_PIN (пін, розділ 4 environment.h,
// напр. src-st7789/environment.h:101), перевірений усюди в src/main.cpp
// (5 місць) як "#if LIGHT_SENSOR_PIN > 0", той самий трюк undefined -> 0,
// що й вище. Виведено сюди один раз, щоб main.cpp гейтив і код, і
// discovery-видимість ОДНИМ прапорцем, а не дублював умову "> 0" у шостому
// місці (features.h) поверх наявних п'яти.
#ifndef LIGHT_SENSOR_PIN
#define LIGHT_SENSOR_PIN 0
#endif
#if LIGHT_SENSOR_PIN > 0
#define HAS_LIGHT_SENSOR 1
#else
#define HAS_LIGHT_SENSOR 0
#endif

#define FEATURE_LIST(X) \
  X(BOARD_HAS_DISPLAY)  \
  X(BOARD_HAS_TOUCHSCREEN) \
  X(BOARD_HAS_SD)        \
  X(BOARD_HAS_IMU)       \
  X(BOARD_HAS_PSRAM)     \
  X(HAS_DINO_GAME)       \
  X(HAS_ECOFLOW_CLIENT)  \
  X(HAS_WEB_PORTAL)      \
  X(HAS_LIGHT_SENSOR)

namespace features {

struct Entry {
  const char* name;
  bool active;
};

// Назва рядка - буквальне ім'я макроса (не перейменований варіант): друге
// джерело назви того самого прапорця розходиться саме так, як розійшлись
// "OPEN"/"open" у encryptionName() (CLAUDE.md, DRY). Дрібний, fixed-size
// масив (8 записів) - PROGMEM тут не потрібен, розмір на порядки менший за
// поріг, коли розміщення в RAM/DRAM (esp8266) взагалі помітне.
#define FEATURE_ENTRY(flag) {#flag, static_cast<bool>(flag)},
static const Entry kAll[] = {FEATURE_LIST(FEATURE_ENTRY)};
#undef FEATURE_ENTRY

static constexpr size_t kCount = sizeof(kAll) / sizeof(kAll[0]);

}  // namespace features
