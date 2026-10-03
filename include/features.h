#pragma once

#include <stddef.h>  // size_t у features::kCount - заголовок має бути самодостатнім

// Єдиний каталог усіх BOARD_HAS_*/HAS_* прапорців, що будь-коли з'являються
// в "Можливості та фічі" (розділ 2) будь-якого src-<env>/environment.h
// (CLAUDE.md, "environment.h кожної плати"). Розділення по 8 environment.h
// загубило спільну картину "які фічі взагалі є в проєкті" - цей файл її
// повертає, без окремого Python-генератора.
//
// Свідомо НЕ включає прапорці, що й досі самі себе визначають через
// __has_include() у власному бібліотечному заголовку - на цей момент таких
// не лишилось (HAS_PING, HAS_GMAIL_SENDER, HAS_MQTT_CLIENT і
// HAS_CONSOLE_MQTT переведені на явний прапорець у `src-<env>/environment.h`,
// розділ 2, + build-fail у `src/Net/Ping.cpp`/`lib/GmailSender/GmailSender.hpp`/
// `lib/MqttClient/MqttClient.hpp`/`lib/ConsoleMqtt/ConsoleMqtt.hpp`, якщо
// значення розійдеться з наявністю бібліотеки/платформи). Абзац лишається на
// випадок, якщо такий прапорець з'явиться знову: показати його тут було б
// хитким - макрос має бути визначений ДО цього заголовка, інакше мовчки "0"
// (undefined -> 0 - навмисно безпечно для #if нижче, але оманливо, якщо
// реальний стан платформи саме "не визначено ще").
//
// HAS_SCREEN_MIRROR - інша категорія: не self-detecting і не пряме поле
// environment.h, а похідне з DISPLAY_SPLIT_COUNT/SPRITE_COLOR_DEPTH (розділ 4
// environment.h) - той самий підхід, що BOARD_HAS_LIGHT_SENSOR нижче.
// Обчислення перенесено сюди з Display.h (де воно раніше й жило,
// помилково описане в цьому коментарі як self-detecting) - lib/Display/Display.hpp тепер
// лише підключає цей файл.
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
// Явний, а не похідний від піна (як BOARD_HAS_LIGHT_SENSOR нижче): на
// esp32-c6 BAT_ADC сидить на GPIO0, і трюк "PIN > 0" його б загубив.
#ifndef BOARD_HAS_BATTERY_ADC
#define BOARD_HAS_BATTERY_ADC 0
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
#ifndef HAS_PING
#define HAS_PING 0
#endif
#ifndef HAS_GMAIL_SENDER
#define HAS_GMAIL_SENDER 0
#endif
#ifndef HAS_MQTT_CLIENT
#define HAS_MQTT_CLIENT 0
#endif
#ifndef HAS_CONSOLE_MQTT
#define HAS_CONSOLE_MQTT 0
#endif
#ifndef HAS_SD_WORKBENCH
#define HAS_SD_WORKBENCH 0
#endif
#ifndef HAS_SD_MSC
#define HAS_SD_MSC 0
#endif

// BOARD_HAS_LIGHT_SENSOR - похідний прапорець, не прямий запис у
// environment.h (BOARD_HAS_, не HAS_, - апаратна ознака плати, той самий
// клас, що BOARD_HAS_IMU/BOARD_HAS_SD, а не софт-фіча). Джерело -
// LIGHT_SENSOR_PIN (пін, розділ 4 environment.h, напр.
// src-st7789/environment.h:101), перевірений усюди в src/main.cpp
// (5 місць) як "#if BOARD_HAS_LIGHT_SENSOR" (було "#if LIGHT_SENSOR_PIN > 0" -
// той самий трюк undefined -> 0, що й вище). Виведено сюди один раз, щоб
// main.cpp гейтив і код, і discovery-видимість ОДНИМ прапорцем, а не
// дублював умову "> 0" у шостому місці (features.h) поверх наявних п'яти.
#ifndef LIGHT_SENSOR_PIN
#define LIGHT_SENSOR_PIN 0
#endif
#if LIGHT_SENSOR_PIN > 0
#define BOARD_HAS_LIGHT_SENSOR 1
#else
#define BOARD_HAS_LIGHT_SENSOR 0
#endif

// HAS_SCREEN_MIRROR - похідний прапорець (той самий підхід, що
// BOARD_HAS_LIGHT_SENSOR вище): дзеркало екрана у веб-порталі (lib/ScreenMirror)
// можливе лише там, де кадр збирається у спрайті RGB565 - без спрайта
// (esp32-c3, DISPLAY_SPLIT_COUNT=0) буфера немає взагалі, а 1bpp-гілка
// (SSD1306 на esp8266) не має що показати браузеру. #ifndef-дефолти нижче -
// той самий трюк undefined -> 0, що й вище, хоча на практиці обидва макроси
// вже задані в кожному environment.h, розділ 4.
#ifndef DISPLAY_SPLIT_COUNT
#define DISPLAY_SPLIT_COUNT 0
#endif
#ifndef SPRITE_COLOR_DEPTH
#define SPRITE_COLOR_DEPTH 0
#endif
#if DISPLAY_SPLIT_COUNT > 0 && SPRITE_COLOR_DEPTH == 16
#define HAS_SCREEN_MIRROR 1
#else
#define HAS_SCREEN_MIRROR 0
#endif

#define FEATURE_LIST(X) \
  X(BOARD_HAS_DISPLAY)  \
  X(BOARD_HAS_TOUCHSCREEN) \
  X(BOARD_HAS_SD)        \
  X(BOARD_HAS_IMU)       \
  X(BOARD_HAS_PSRAM)     \
  X(BOARD_HAS_LIGHT_SENSOR) \
  X(BOARD_HAS_BATTERY_ADC) \
  X(HAS_DINO_GAME)       \
  X(HAS_ECOFLOW_CLIENT)  \
  X(HAS_WEB_PORTAL)      \
  X(HAS_PING)            \
  X(HAS_GMAIL_SENDER)    \
  X(HAS_MQTT_CLIENT)     \
  X(HAS_CONSOLE_MQTT)    \
  X(HAS_SD_WORKBENCH)    \
  X(HAS_SD_MSC)          \
  X(HAS_SCREEN_MIRROR)

namespace features {

struct Entry {
  const char* name;
  bool active;
};

// Назва рядка - буквальне ім'я макроса (не перейменований варіант): друге
// джерело назви того самого прапорця розходиться саме так, як розійшлись
// "OPEN"/"open" у encryptionName() (CLAUDE.md, DRY). Дрібний, fixed-size
// масив (17 записів) - PROGMEM тут не потрібен, розмір на порядки менший за
// поріг, коли розміщення в RAM/DRAM (esp8266) взагалі помітне.
#define FEATURE_ENTRY(flag) {#flag, static_cast<bool>(flag)},
static const Entry kAll[] = {FEATURE_LIST(FEATURE_ENTRY)};
#undef FEATURE_ENTRY

static constexpr size_t kCount = sizeof(kAll) / sizeof(kAll[0]);

}  // namespace features
