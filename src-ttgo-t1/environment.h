// environment.h
// Компіляційні налаштування плати ttgo-t1 (LillyGo T-Display, класичний
// ESP32, ST7789 SPI 135x240 через bodmer/TFT_eSPI, без PSRAM, без SD).
// Порядок розділів і правило оформлення - CLAUDE.md, розділ
// "environment.h кожної плати".
//
// Підключається через build_flags у platformio.ini (env:ttgo-t1):
//   -include src-ttgo-t1/environment.h
// На відміну від esp32-st7789/esp32-s3-lcd147, окремого include/Setup_*.h
// тут немає - усі TFT_eSPI-макроси (USER_SETUP_LOADED/ST7789_DRIVER/пини)
// живуть просто тут, у розділі 4.

#pragma once

// ============================================================
// 1. Ідентифікація плати
// ============================================================
#define BOARD_TTGO_T1 1

// ============================================================
// 2. Можливості та фічі (BOARD_HAS_* та HAS_*)
// ============================================================
#define BOARD_HAS_DISPLAY 1
#define BOARD_HAS_TOUCHSCREEN 0
#define BOARD_HAS_SD 0

#define HAS_DINO_GAME 1

// Веб-портал (lib/WebPortal) - опис механізму в env:esp32-c6. Як і на
// esp32-st7789: класичний ESP32 без PSRAM, флешу вдосталь (app 3 МБ),
// ризик - купа. Саме через неї разом з увімкненням порталу довелось
// вирішувати конфлікт з EcoFlow (нижче).
#define HAS_WEB_PORTAL 1

// EcoFlow (HAS_ECOFLOW_CLIENT) увімкнено разом із порталом. Заміряно на
// ЦІЙ платі 15.09.2026 (тест M35), не виведено за аналогією:
//   лише портал        : largest block 57.3 КБ, EcoFlow немає у збірці
//   лише EcoFlow        : connected = yes, 11 повідомлень за 10 с, дані йдуть
//   портал + EcoFlow (прямий TLS) : largest block 32.7 КБ ->
//                        'SSL - Memory allocation failed' (-32512) у
//                        ssl_client.cpp:41, далі 'MQTT connect fail' вічно
// Однаково і при автоконекті на старті, і при ручному 'ecoflow-start':
// AsyncTCP забирає своє ще в setup(), тому порядок нічого не рятує.
// TLS-сесія EcoFlow потребує ~57 КБ ОДНИМ блоком, а плата без PSRAM.
// Дивитись 'largest block', а не 'free': вільного 32-108 КБ у всіх трьох.
// Розв'язано MQTT-проксі (розділ 5) - той сам тримає TLS-сесію на rpi5,
// плата ходить лише plain MQTT. Як помістити обидва без проксі -
// docs/tech_debt.md, розділ 4.
#define HAS_ECOFLOW_CLIENT 1

// Пінг (src/ping.h) - опис принципу в env:esp32-4848s040. dvarrel/ESPping
// присутній у lib_deps цього env.
#define HAS_PING 1

// Gmail (lib/GmailSender) - опис принципу в env:esp32-4848s040.
// mobizt/ReadyMail присутній у lib_deps цього env.
#define HAS_GMAIL_SENDER 1

// ============================================================
// 3. Системні піни та налаштування
// ============================================================

// Кільце журналу (lib/Journal): 32 записів x ~180 Б.
#define JOURNAL_RING 32

// Вирішуємо проблему сміття в моніторі.
#define CONFIG_ESP32_DEFAULT_CPU_FREQ_240 y

// ============================================================
// 4. Піни та налаштування окремих сутностей/датчиків
// ============================================================

// ---------- Дисплей: ST7789 135x240 (bodmer/TFT_eSPI) ----------
#define USER_SETUP_LOADED 1
#define ST7789_DRIVER 1
#define TFT_WIDTH 135
#define TFT_HEIGHT 240
#define CGRAM_OFFSET 1
#define TFT_MOSI 19
#define TFT_SCLK 18
#define TFT_CS 5
#define TFT_DC 16
#define TFT_RST 23
#define TFT_BL 4
#define TFT_BACKLIGHT_ON 1
#define TFT_ROTATION 3

#define LOAD_GLCD 1
#define LOAD_FONT2 1
#define LOAD_FONT3 1
#define LOAD_FONT4 1
#define LOAD_FONT5 1
#define LOAD_FONT6 1
#define LOAD_FONT7 1
#define LOAD_FONT8 1
#define LOAD_FONT9 1
#define SPI_FREQUENCY 40000000

#define SPRITE_COLOR_DEPTH 16

// 3, а не 2: height() тут 135, і 135/2=67 -> смуги покривають рядки 0..133,
// а рядок 134 не потрапляє В ЖОДНУ смугу і не перемальовується ніколи (там
// лишається вміст VRAM). 135 = 3*45 ділиться рівно; бонусом спрайт меншає
// з 240*67*2=32160 B до 240*45*2=21600 B. Та сама пастка, що описана для
// esp32-c6 (розділ 4 там же).
#define DISPLAY_SPLIT_COUNT 3

// Фонове зображення - три взаємовиключні варіанти (повна матриця по всіх
// платах - docs/architecture.md, розділ "Матриця фіч по платах"). На цій
// платі жоден не увімкнений (варіант 2 лишається закомментованою готовою
// альтернативою нижче).
#define BACKGROUND_IMAGES_COUNT 0
// #define LITTLEFS_BACKGROUND_IMAGE "/space-240x135.jpg"

// ---------- Тач (відсутній, BOARD_HAS_TOUCHSCREEN=0) ----------
// TOUCH_CS=0 - НЕ реальний тач, лише придушення warning бібліотеки TFT_eSPI
// (той самий прийом, що на esp32-s3-lcd147).
#define TOUCH_CS 0

// ---------- Кнопка ----------
#define FLIP_BUTTON_PIN 0  // BOOT button is tied to GPIO 0 (зверху)
// #define FLIP_BUTTON_PIN 35  // bottom knob GPIO 35 (збоку від USB type-c, знизу)

// ============================================================
// 5. Налаштування фіч
// ============================================================

// EcoFlow (HAS_ECOFLOW_CLIENT, розділ 2).
#define ECOFLOW_AUTOCONNECT 1
#define ECOFLOW_SYNC_ON_BOOT 1
// ECOFLOW_MQTT_PROXY_HOST/USERNAME/PASSWORD лишаються в platformio.ini:
// усі три беруться з secrets.ini (${secrets.*}) - заголовок не бачить
// змінних PlatformIO.
