// environment.h
// Компіляційні налаштування плати esp32-c6-lcd096 ("ESP32-C6-LCD-0.96",
// ST7735S 160x80, без touch/IMU). Порядок розділів і правило оформлення -
// CLAUDE.md, розділ "environment.h кожної плати".
//
// Підключається через build_flags у platformio.ini (env:esp32-c6-lcd096):
//   -include src-esp32-c6-lcd096/environment.h

#pragma once

// ============================================================
// 1. Ідентифікація плати
// ============================================================
#define BOARD_ESP32_C6_LCD096 1

// ============================================================
// 2. Можливості та фічі (BOARD_HAS_* та HAS_*)
// ============================================================
#define BOARD_HAS_DISPLAY 1
#define BOARD_HAS_TOUCHSCREEN 0
#define BOARD_HAS_SD 1

#define HAS_DINO_GAME 1

// Веб-портал (lib/WebPortal) - див. docs/web_portal.md. Тут 4 МБ флешу,
// app-розділ 2 МБ: портал коштує ~57 КБ, запас лишається понад 300 КБ.
#define HAS_WEB_PORTAL 1

// EcoFlow - як і esp32-c6/esp32-c3, через MQTT-проксі на rpi5
// (ECOFLOW_MQTT_PROXY_* в platformio.ini для цього env): реєстрація акаунта
// й ACL для НОВОЇ плати - docs/ecoflow_mqtt_proxy_setup.md, "Реєстрація
// нової плати" (Крок A + Крок B, обов'язково обидва). Runtime-override
// нижче, розділ 5.
#define HAS_ECOFLOW_CLIENT 1

// ============================================================
// 3. Системні піни та налаштування
// ============================================================

// Кільце журналу (lib/Journal): 32 записів x ~180 Б.
#define JOURNAL_RING 32
#define CORE_DEBUG_LEVEL 2

#define ARDUINO_USB_MODE 1
#define ARDUINO_USB_CDC_ON_BOOT 1

// Дисплей і TF-картка на ОДНІЙ SPI-шині (SCK=7/MOSI=6). Вмикає в
// src/main.cpp RAII-дужку, яка віддає шину дисплея на час звернення до
// картки - інакше будь-який доступ до SD з-під відкритої транзакції
// дисплея (loop() тримає її на весь кадр) вішає плату намертво на не
// рекурсивному мьютексі SPIClass::paramLock.
//
// УВАГА: прапорець ширший, ніж каже його історія. Дужку потрібно тримати
// не лише через SD: transaction-лок бере і сам Arduino_GFX (напр.
// Arduino_ST7735::setRotation() у display_flip()), тому без неї команда
// flip з консолі вішала плату намертво - без шансу на watchdog, бо задача
// коректно блокується на семафорі.
#define DISPLAY_BUS_YIELD 1

// ============================================================
// 4. Піни та налаштування окремих сутностей/датчиків
// ============================================================

// ---------- Дисплей: ST7735S 160x80 ----------
// Піни SPI самого дисплея (SCLK/MOSI/CS/DC/RST/BL) - в
// include/Setup_ST7735_C6_LCD096.h. TFT_WIDTH/TFT_HEIGHT там навмисно
// закомментовані - джерело значень лише тут (на відміну від esp32-c6, де
// Setup_JD9853_C6.h теж їх визначає).
#define TFT_WIDTH 80
#define TFT_HEIGHT 160

// TFT_ROTATION=1 - НЕ перевірено на реальному пристрої (панель нативно
// 160x80, на відміну від портретних панелей esp32-c6/esp32-s3-lcd147,
// тому, можливо, взагалі не потребує повороту - уточнити емпірично, як і
// в інших C6-платах проєкту).
#define TFT_ROTATION 1
#define SPRITE_COLOR_DEPTH 16

// Дисплей малий (160x80x2 = 25 600 байт) - на відміну від інших плат,
// спліт екрана на кілька спрайтів не потрібен навіть без PSRAM.
#define DISPLAY_SPLIT_COUNT 2

// Шрифти. На цій платі НЕ TFT_eSPI, а Arduino_GFX, тому прапорці читає наш
// шар сумісності include/ArduinoGfxFonts.h (там і таблиця відповідності
// номерів). Раніше вони стояли тут без значень і без ефекту взагалі -
// справжній TFT_eSPI їх не бачив, бо його тут немає.
//
// Екран лише 160x80 і app-розділ 1.5 MB, тому ввімкнено ощадливо: FONT4
// (26 px) - максимум, що має сенс на такій висоті; FONT7 (42 px) займає
// майже весь екран, лишений для великого годинника. FONT6/FONT8 не
// реалізовані (див. ArduinoGfxFonts.h).
#define LOAD_GLCD 1
#define LOAD_FONT2 0
#define LOAD_FONT4 0
#define LOAD_FONT7 0
// U8G2_FONT_SUPPORT вмикає в Arduino_GFX перевантаження setFont(const
// uint8_t*) + setUTF8Print() - без них 7-сегментний шрифт (FONT7)
// підключити нічим. Сама бібліотека U8g2 НЕ потрібна: Arduino_GFX має
// власний декодер u8g2-шрифтів; прапорець задається напряму замість
// підключення залежності заради одного масиву.
#define U8G2_FONT_SUPPORT 1

// Годинник і дата на екрані.
#define CLOCK_TEXT_FONT 1
#define CLOCK_TEXT_SIZE 3
// CLOCK_POS_X не задано - auto
#define CLOCK_POS_Y 18
#define DATE_TEXT_FONT 1
#define DATE_TEXT_SIZE 2
// DATE_POS_X не задано - auto
#define DATE_POS_Y 50

// Фонове зображення - три взаємовиключні варіанти (повна матриця по всіх
// платах - docs/architecture.md, розділ "Матриця фіч по платах"):
//   1) BACKGROUND_IMAGES_COUNT >= 1 - вшита в прошивку картинка, 0 байт heap.
//   2) LITTLEFS_BACKGROUND_IMAGE="/файл.jpg" - декодується в RAM при старті.
//   3) BACKGROUND_PROGMEM_HEADER="шлях.h" - RGB565-масив запечений у Flash.
// На цій платі: BACKGROUND_IMAGES_COUNT=0, а LITTLEFS_BACKGROUND_IMAGE
// УВІМКНЕНО (варіант 2), хоча сам файл під 160x80 ще НЕ підготовлено
// (потрібна конвертація) - шлях лишається заданим на майбутнє.
#define BACKGROUND_IMAGES_COUNT 0
#define LITTLEFS_BACKGROUND_IMAGE "/background-01-160x80.jpg"

// ---------- TF-картка ----------
// esp32-c6-lcd096 - CS=4, SCK=7, MISO=5, MOSI=6 (зі схеми плати). Шина
// SCK/MOSI спільна з дисплеєм - див. DISPLAY_BUS_YIELD у розділі 3.
#define SD_CS 4
#define SD_MOSI 6
#define SD_MISO 5
#define SD_SCK 7
#define SD_FREQ 4000000

// ---------- Кнопка ----------
#define FLIP_BUTTON_PIN 9

// ============================================================
// 5. Налаштування фіч
// ============================================================

// EcoFlow (HAS_ECOFLOW_CLIENT, розділ 2). 1 - піднімати MQTT одразу на
// старті; 0 - лише за командою 'ecoflow-start'. Runtime-override:
// ConfigStorage ключ 'ecoflow.auto'/'ecoflow.sync'.
#define ECOFLOW_AUTOCONNECT 1
#define ECOFLOW_SYNC_ON_BOOT 1

// Тестове розгортання плану "спільний MqttClient" (docs/tech_debt.md,
// "План: спільний MqttClient для mqtt + EcoflowClient") - на цій платі
// mqtt_username/password (загальний клієнт) і ecoflow_proxy_username/
// password (проксі) уже сьогодні той самий аліас у secrets.ini, тому
// EcoflowClient не піднімає власне з'єднання, а користується вже живим
// `mqtt`. Прапорець явний і локальний для цієї плати (не автовизначення
// збігу кредів у рантаймі) - інші проксі-плати цим ще не зачіпаються.
#define ECOFLOW_MQTT_SHARE_CLIENT 1
