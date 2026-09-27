// environment.h
// Компіляційні налаштування плати esp8266 (NodeMCU, SSD1306 128x64 OLED,
// PubSubClient замість PicoMQTT). Порядок розділів і правило оформлення -
// CLAUDE.md, розділ "environment.h кожної плати".
//
// Підключається через build_flags у platformio.ini (env:esp8266):
//   -include src-esp8266/environment.h

#pragma once

// ============================================================
// 1. Ідентифікація плати
// ============================================================
#define BOARD_ESP8266 1

// ============================================================
// 2. Можливості та фічі (BOARD_HAS_* та HAS_*)
// ============================================================
#define BOARD_HAS_DISPLAY 1
#define BOARD_HAS_TOUCHSCREEN 0
#define BOARD_HAS_SD 0

// MQTT-клієнт (lib/MqttClient) - опис принципу в env:esp32-4848s040. Тут -
// PubSubClient (єдиний env на ньому, не PicoMQTT), присутній у lib_deps.
#define HAS_MQTT_CLIENT 1

// Дзеркало консолі в MQTT (lib/ConsoleMqtt) вимкнено - механізм прив'язаний
// до ESP32+PicoMQTT (опис принципу в env:esp32-4848s040), на PubSubClient
// publish() синхронний і без окремого мережевого таска logging-помпи не
// підходить.
#define HAS_CONSOLE_MQTT 0

// Chrome Dino на екрані: команда "dino on|off" (src/Dino/, lib/DinoGame).
// Екран монохромний 128x64 - DinoSprites.h бере малий набір асетів
// (DINO_ASSET_TIER=1, діно 22x24), а kBg/kFg у DinoRenderer.cpp свідомо
// TFT_BLACK/TFT_WHITE, які тут дорівнюють 0/1.
#define HAS_DINO_GAME 1

// Веб-портал і EcoFlow тут ВИМКНЕНІ (=0, не просто відсутні) - на цій
// платі лише 80 КБ RAM, і lib_ignore=AsyncTCP у цьому env прибирає саму
// бібліотеку, від якої обидва залежать. main.cpp має власний
// "#ifndef HAS_WEB_PORTAL -> 0" дефолт, а незаданий HAS_ECOFLOW_CLIENT у
// "#if" теж рахується як 0 - явний нуль тут лише для повноти маніфесту.
#define HAS_WEB_PORTAL 0
#define HAS_ECOFLOW_CLIENT 0

// Пінг (src/ping.h) вимкнено - ESP8266Ping.h НЕ вбудований у поточний
// ESP8266 Arduino core (перевірено `find` по системі - файла немає
// ніде) і не доданий у lib_deps цього env: старий коментар у ping.h
// ("вбудований у core") був неправдивим - HAS_PING=1 тут падав би
// #error у src/ping.h з першої ж збірки, саме такою й мала бути реакція.
#define HAS_PING 0

// Gmail (lib/GmailSender) - опис принципу в env:esp32-4848s040.
// mobizt/ReadyMail присутній у lib_deps цього env.
#define HAS_GMAIL_SENDER 1

// ============================================================
// 3. Системні піни та налаштування
// ============================================================

// Кільце журналу (lib/Journal): 8 записів x ~180 Б - межа, не вибір: на
// esp8266 лише 80 КБ RAM (docs/architecture.md).
#define JOURNAL_RING 8

// Черга команд (lib/CommandQueue) - спільний ліміт "скільки команд у
// польоті" для serial/MQTT SAPI/порталу/cron разом. Фактичний троттлінг
// SAPI - на боці браузера (sapi/index.html, SAPI_MAX_INFLIGHT=2), не тут;
// це число тримають БІЛЬШИМ за нього там, де вистачає RAM (з запасом на
// решту джерел - інакше SAPI сам вичерпував би всю чергу), а на
// найтісніших платах - рівним йому, свідомо. Повне пояснення й таблиця
// по всіх платах - docs/mqtt-web-handoff.md, "SAPI: обмеження одночасних
// запитів".
#define COMMAND_QUEUE_SLOTS 2

// ============================================================
// 4. Піни та налаштування окремих сутностей/датчиків
// ============================================================

// ---------- Дисплей: SSD1306 128x64 (I2C, TFT_eSPI-фасад) ----------
// SDA_PIN/SCL_PIN - в include/Setup_SSD1306_NodeMCU.h (константи, не -D).
#define TFT_WIDTH 128
#define TFT_HEIGHT 64
#define OLED_WIDTH 128
#define OLED_HEIGHT 64
#define OLED_RESET_PIN -1
#define OLED_I2C_ADDR 0x3C
#define TFT_ROTATION 2
#define SPRITE_COLOR_DEPTH 1
#define DISPLAY_SPLIT_COUNT 1

// Фонове зображення - тут вимкнене повністю (варіант 2, LittleFS, спробувано
// й відхилено): картинка "зносить дах" монохромному дисплею.
#define BACKGROUND_IMAGES_COUNT 0
// #define LITTLEFS_BACKGROUND_IMAGE "/space-240x135.jpg"  // у дісплея кришу зносить...

// ---------- Кнопка ----------
#define FLIP_BUTTON_PIN 0  // The BOOT button is tied to GPIO 0

// ---------- LED ----------
#define BLINK_LED_PIN 2  // D4, вбудований LED
