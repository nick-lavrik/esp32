// environment.h
// Компіляційні налаштування плати esp32-st7789 (класичний ESP32, ST7789 SPI
// 240x320 через bodmer/TFT_eSPI, XPT2046 touch, без PSRAM). Порядок розділів
// і правило оформлення - CLAUDE.md, розділ "environment.h кожної плати".
//
// Підключається через build_flags у platformio.ini (env:esp32-st7789):
//   -include src-st7789/environment.h
// Поруч лишається ОКРЕМИЙ -include include/Setup_ST7789.h - там уже
// визначені USER_SETUP_LOADED/ST7789_DRIVER/TFT_RGB_ORDER/TFT_WIDTH/
// TFT_HEIGHT і піни SPI дисплея MISO/MOSI/SCLK/CS/DC/RST - єдине джерело
// істини для НИХ, тому тут вони не повторюються. TFT_BL/TFT_BACKLIGHT_ON
// у тому заголовку навмисно закомментовані (шаблон "звірте з розводкою") -
// на цій платі підсвітка є, тому обидва задані нижче, у розділі 4.

#pragma once

// ============================================================
// 1. Ідентифікація плати
// ============================================================
#define BOARD_ST7789 1
// Назва плати для MQTT discovery (поле "board", src/Mqtt/Discovery.cpp).
#define BOARD_NAME "esp32-st7789"

// ============================================================
// 2. Можливості та фічі (BOARD_HAS_* та HAS_*)
// ============================================================
#define BOARD_HAS_DISPLAY 1
#define BOARD_HAS_TOUCHSCREEN 1
#define BOARD_HAS_SD 1
// Важкий SD-інструментарій (sdprobe/sdraw/sdbench/sdmap/sdimg/...) - опис у
// env:esp32-s3-lcd147. Тут вимкнено: картка лише носій (status sd, System).
#define HAS_SD_WORKBENCH 0

// MQTT-клієнт (lib/MqttClient) - опис принципу в env:esp32-4848s040.
// PicoMQTT присутній у lib_deps цього env.
#define HAS_MQTT_CLIENT 1

// Дзеркало консолі в MQTT (lib/ConsoleMqtt) - опис принципу в env:esp32-4848s040.
#define HAS_CONSOLE_MQTT 1

// Прапорець ФІЧІ, не драйвера - вмикається лише там, де є дисплей.
#define HAS_DINO_GAME 1

// Веб-портал (lib/WebPortal) - опис механізму в env:esp32-c6. Класичний
// ESP32 БЕЗ PSRAM: вузьке місце тут не флеш (лишається ~320 КБ в
// app-розділі), а купа - AsyncTCP тримає свій таск і буфери на кожне
// з'єднання. Перевіряти саме heap: 'heap' до і після відкриття сторінки.
#define HAS_WEB_PORTAL 1

// EcoFlow (HAS_ECOFLOW_CLIENT) увімкнено разом із порталом, і це заміряно
// на залізі 15.09.2026, а не забуто. Умова механізму (PicoMQTT) виконана,
// впирається в купу: TLS-сесія EcoFlow потребує ~57 КБ ОДНИМ блоком, а
// плата без PSRAM.
//   портал + EcoFlow : largest block 32.7 КБ -> 'MQTT connect fail' вічно
//   лише EcoFlow      : largest block 59.4 КБ -> connected = yes, дані йдуть
//   портал + сплітів 12 (менший спрайт): 51.2 КБ -> усе одно не вистачає
// Дивитись треба 'largest block', а не 'free': вільного там 64-88 КБ.
// Розв'язано MQTT-проксі (розділ 5) - той сам тримає TLS-сесію на rpi5,
// плата ходить лише plain MQTT. Як помістити обидва без проксі -
// docs/tech_debt.md, розділ 4.
#define HAS_ECOFLOW_CLIENT 1

// Пінг (src/ping.h) вимкнено - dvarrel/ESPping закоментовано в lib_deps
// цього env. Опис принципу в env:esp32-4848s040.
#define HAS_PING 0

// Gmail (lib/GmailSender) вимкнено - mobizt/ReadyMail закоментовано в
// lib_deps цього env. Опис принципу в env:esp32-4848s040.
#define HAS_GMAIL_SENDER 0

// ============================================================
// 3. Системні піни та налаштування
// ============================================================

// Кільце журналу (lib/Journal): 32 записів x ~180 Б.
#define JOURNAL_RING 32
#define CORE_DEBUG_LEVEL 0  // 2
// #define CONFIG_BT_ENABLED 0

// Черга команд (lib/CommandQueue) - спільний ліміт "скільки команд у
// польоті" для serial/MQTT SAPI/порталу/cron разом. Фактичний троттлінг
// SAPI - на боці браузера (sapi/index.html, SAPI_MAX_INFLIGHT=2), не тут;
// це число тримають БІЛЬШИМ за нього там, де вистачає RAM (з запасом на
// решту джерел - інакше SAPI сам вичерпував би всю чергу), а на
// найтісніших платах - рівним йому, свідомо. Повне пояснення й таблиця
// по всіх платах - docs/mqtt-web-handoff.md, "SAPI: обмеження одночасних
// запитів".
#define COMMAND_QUEUE_SLOTS 2

// Принудительно заставляем дисплей використовувати HSPI (не VSPI) - SD-картка
// лишається на іншій шині, тому DISPLAY_BUS_YIELD тут НЕ потрібен (на
// відміну від C6-плат, де дисплей і SD спільну шину ділять).
#define USE_HSPI_PORT 1

// ============================================================
// 4. Піни та налаштування окремих сутностей/датчиків
// ============================================================

// ---------- Дисплей: ST7789 240x320 (пини - include/Setup_ST7789.h) ----------
#define TFT_BL 21
#define TFT_BACKLIGHT_ON 1
#define TFT_ROTATION 3
#define TFT_INVERSION_OFF 1
#define SPI_FREQUENCY 40000000  // 20000000 / 27000000 / 40000000

// TFT_eSPI перевіряє шрифти через #ifdef, тож "#define LOAD_FONT4 0" шрифт
// НЕ вимикає - вимкнути можна лише прибравши рядок. Колишні "0" нижче нічого
// не міняли (і заміри heap поруч з ними - теж: шрифти лежать у flash, не в
// heap), тому тут записано фактичний стан. Прибирати рядок - лише разом із
// перевіркою, що цей шрифт ніде не малюється (drawSystemInfo() - setTextFont(1)).
#define LOAD_GLCD 1
#define LOAD_FONT2 1
#define LOAD_FONT4 1
#define LOAD_FONT6 1
#define LOAD_FONT7 1
#define LOAD_FONT8 1

#define SPRITE_COLOR_DEPTH 16  // 8 => кольорово але убого
// #define DISPLAY_SPLIT_COUNT 6  // 240 / 6 = 40
#define DISPLAY_SPLIT_COUNT 12  // 240 / 12 = 20

// Фонове зображення - три взаємовиключні варіанти (повна матриця по всіх
// платах - docs/architecture.md, розділ "Матриця фіч по платах"):
//   1) BACKGROUND_IMAGES_COUNT >= 1 - вшита в прошивку картинка, 0 байт heap.
//   2) LITTLEFS_BACKGROUND_IMAGE="/файл.jpg" - декодується в RAM при старті.
//   3) BACKGROUND_PROGMEM_HEADER="шлях.h" - RGB565-масив запечений у Flash.
// На цій платі: варіант 1 (3 вшиті картинки), пам'яті вистачає на все.
#define BACKGROUND_IMAGES_COUNT 1  // 3 - працює "ок", пам'яті вистачає на все
// #define LITTLEFS_BACKGROUND_IMAGE "/space-02.jpg"
// #define LITTLEFS_BACKGROUND_IMAGE "/space-240x135.jpg"  // у дісплея кришу зносить...

// ---------- Тач XPT2046 ----------
#define TOUCH_CS 33

// ---------- Датчик світла ----------
#define LIGHT_SENSOR_PIN 34

// ---------- TF-картка ----------
// esp32-st7789 - CS=5, SCK=18, MISO=19, MOSI=23
#define SD_CS 5
#define SD_MOSI 23
#define SD_MISO 19
#define SD_SCK 18
// #define SD_FREQ 400000  // success
// #define SD_FREQ 800000  // success
#define SD_FREQ 400000

// ---------- Кнопка ----------
#define PRIMARY_BUTTON_PIN 0  // The BOOT button is tied to GPIO 0

// ============================================================
// 5. Налаштування фіч
// ============================================================

// EcoFlow (HAS_ECOFLOW_CLIENT, розділ 2).
#define ECOFLOW_AUTOCONNECT 1
#define ECOFLOW_SYNC_ON_BOOT 0
// ECOFLOW_MQTT_PROXY_HOST/USERNAME/PASSWORD лишаються в platformio.ini:
// усі три беруться з secrets.ini (${secrets.*}) - заголовок не бачить
// змінних PlatformIO.
#define ECOFLOW_MQTT_SHARE_CLIENT 1
