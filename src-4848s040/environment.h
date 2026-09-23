// environment.h
// Компіляційні налаштування плати esp32-4848s040 (ESP32-S3-4848S040-LCD-4.0,
// ST7701 480x480 + GT911 touch, 8 МБ OPI PSRAM). Порядок розділів і правило
// оформлення - CLAUDE.md, розділ "environment.h кожної плати".
//
// Підключається через build_flags у platformio.ini (env:esp32-4848s040):
//   -include src-4848s040/environment.h

#pragma once

// ============================================================
// 1. Ідентифікація плати
// ============================================================
#define BOARD_4848S040 1

// ============================================================
// 2. Можливості та фічі (BOARD_HAS_* та HAS_*)
// ============================================================
#define BOARD_HAS_PSRAM 1
#define BOARD_HAS_DISPLAY 1
#define BOARD_HAS_TOUCHSCREEN 1
#define BOARD_HAS_SD 1

// MQTT-клієнт (lib/MqttClient) - транспорт для загального `mqtt` (SAPI JSON
// API, discovery, console-дзеркало) і, окремо, EcoflowClient. Явний
// прапорець, не __has_include(<PubSubClient.h>)/__has_include(<PicoMQTT.h>)
// в самому MqttClient.hpp - той самий принцип, що й HAS_PING/HAS_GMAIL_SENDER
// вище: lib/MqttClient/MqttClient.hpp провалить збірку #error, якщо значення
// розійдеться з наявністю бібліотеки в lib_deps нижче (тут - PicoMQTT).
#define HAS_MQTT_CLIENT 1

// Дзеркало консолі в MQTT (lib/ConsoleMqtt) - явний прапорець, як
// HAS_MQTT_CLIENT вище. Прив'язаний до ESP32+PicoMQTT (лише там publish()
// не блокує помпу) - lib/ConsoleMqtt/ConsoleMqtt.hpp провалить збірку
// #error, якщо HAS_CONSOLE_MQTT=1 на платформі без цієї пари.
#define HAS_CONSOLE_MQTT 1

// Chrome Dino на екрані: команда "dino on|off" (src/Dino/, lib/DinoGame).
// Тут екран 480x480, тому DinoSprites.h сам бере DINO_SPRITE_SCALE=3 -
// інакше діно заввишки 47 px виглядав би мурахою.
#define HAS_DINO_GAME 1

// Веб-портал (lib/WebPortal) - опис механізму в env:esp32-c6. Тут він
// найдешевший з усіх плат: 8 МБ OPI PSRAM, app-розділ 4 МБ.
#define HAS_WEB_PORTAL 1

// Телеметрія EcoFlow (src/Ecoflow/) - опис в env:esp32-c6. Умова механізму -
// PicoMQTT (лише в його гілці є MqttClient::suspend(), без якого REST-сесія
// і MQTT-over-TLS не вживаються); тут вона виконана, як і на решті ESP32.
// Heap не вузьке місце: 8 МБ PSRAM.
#define HAS_ECOFLOW_CLIENT 1

// Пінг (src/ping.h, команда 'ping'). Явний прапорець, як HAS_WEB_PORTAL/
// HAS_ECOFLOW_CLIENT вище - src/ping.h провалить збірку #error, якщо
// значення розійдеться з наявністю dvarrel/ESPping у lib_deps нижче.
#define HAS_PING 1

// Gmail (lib/GmailSender/GmailSender.hpp, команда 'smtp-probe' і т.п.).
// Явний прапорець, як HAS_PING/HAS_WEB_PORTAL/HAS_ECOFLOW_CLIENT вище -
// lib/GmailSender/GmailSender.hpp провалить збірку #error, якщо значення
// розійдеться з наявністю mobizt/ReadyMail у lib_deps нижче.
#define HAS_GMAIL_SENDER 1

// ============================================================
// 3. Системні піни та налаштування
// ============================================================

// Кільце журналу (lib/Journal): 48 записів x ~180 Б.
#define JOURNAL_RING 48
#define CORE_DEBUG_LEVEL 2  // 0 - no debug
#define CONFIG_HEAP_POISONING_COMPREHENSIVE 1

// LovyanGFX v1 API (include/Setup_ST7701_4848S040.h).
#define LGFX_USE_V1 1

// ============================================================
// 4. Піни та налаштування окремих сутностей/датчиків
// ============================================================

// ---------- Дисплей: ST7701 480x480 (LovyanGFX) ----------
#define TFT_WIDTH 480
#define TFT_HEIGHT 480
#define TFT_BL 38
#define TFT_ROTATION 2
#define SPRITE_COLOR_DEPTH 16
#define DISPLAY_SPLIT_COUNT 1  // free heap <start/end>: 298816 / 219984
// #define DISPLAY_SPLIT_COUNT 2  // free heap <start/end>: 298816 / 219976
// #define DISPLAY_SPLIT_COUNT 4  // free heap <start/end>: 298816 / 219960

#define LOAD_FONT2 1
#define LOAD_FONT4 1
#define LOAD_FONT6 1
#define LOAD_FONT7 1
#define LOAD_FONT8 1

// Фонове зображення - три взаємовиключні варіанти (повна матриця по всіх
// платах - docs/architecture.md, розділ "Матриця фіч по платах"):
//   1) BACKGROUND_IMAGES_COUNT >= 1 - вшита в прошивку картинка, 0 байт heap.
//   2) LITTLEFS_BACKGROUND_IMAGE="/файл.jpg" - декодується в RAM при старті.
//   3) BACKGROUND_PROGMEM_HEADER="шлях.h" - RGB565-масив запечений у Flash.
// На цій платі: варіант 2, з 8 МБ PSRAM запас великий - інші файли нижче
// лишені як готові альтернативи (перемкнути одним рядком).
#define BACKGROUND_IMAGES_COUNT 0
// #define LITTLEFS_BACKGROUND_IMAGE "/moon-480x480.jpg"
// #define LITTLEFS_BACKGROUND_IMAGE "/background-01-480x480.jpg"  // вишитий шеврон
#define LITTLEFS_BACKGROUND_IMAGE "/background-02-480x480.jpg"  // штурмовик по центру
// #define LITTLEFS_BACKGROUND_IMAGE "/background-03-480x480.jpg"  // білий 2D шеврон

// ---------- Тач GT911 ----------
// I2C. Назви прапорців НАВМИСНО такі самі, як на esp32-c6 (I2C_SDA/I2C_SCL/
// TOUCH_INT/TOUCH_RST) - див. "Єдині назви прапорців" у docs/architecture.md.
// INT/RST на цій платі не розведені.
#define I2C_SDA 19
#define I2C_SCL 45
#define TOUCH_INT -1
#define TOUCH_RST -1

// ---------- TF-картка ----------
// esp32-4848s040 - CS=42, SCK=48, MOSI=47, MISO=41
#define SD_CS 42
#define SD_MOSI 47
#define SD_MISO 41
#define SD_SCK 48
#define SD_FREQ 4000000

// ============================================================
// 5. Налаштування фіч
// ============================================================

// EcoFlow (HAS_ECOFLOW_CLIENT, розділ 2).
#define ECOFLOW_AUTOCONNECT 1
#define ECOFLOW_SYNC_ON_BOOT 1
