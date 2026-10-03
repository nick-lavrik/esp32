// environment.h
// Компіляційні налаштування плати esp32-s3-lcd147 (Waveshare ESP32-S3-LCD-1.47,
// ST7789 SPI 172x320, 8 МБ PSRAM, SD_MMC, без touch). Порядок розділів і
// правило оформлення - CLAUDE.md, розділ "environment.h кожної плати".
//
// Підключається через build_flags у platformio.ini (env:esp32-s3-lcd147):
//   -include src-esp32-s3-lcd147/environment.h
// Поруч лишається ОКРЕМИЙ -include include/Setup_ST7789_lcd147.h - там
// живуть усі TFT_eSPI-специфічні макроси (USER_SETUP_LOADED, ST7789_DRIVER,
// TFT_WIDTH/HEIGHT, піни дисплея, SPI_FREQUENCY, LOAD_FONT*) - єдине джерело
// істини для них, тому тут вони НЕ повторюються.

#pragma once

// ============================================================
// 1. Ідентифікація плати
// ============================================================
#define BOARD_ESP32_S3_LCD147 1
// Назва плати для MQTT discovery (поле "board", src/Mqtt/Discovery.cpp).
#define BOARD_NAME "esp32-s3-lcd147"

// ============================================================
// 2. Можливості та фічі (BOARD_HAS_* та HAS_*)
// ============================================================
#define BOARD_HAS_PSRAM 1
#define BOARD_HAS_DISPLAY 1
#define BOARD_HAS_TOUCHSCREEN 0
#define BOARD_HAS_SD 1
// Важкий SD-інструментарій поверх BOARD_HAS_SD (src/Sd/Sd.hpp): домени
//   SD_PROBE  - sdprobe/sdscan/sdbb: діагностика шини й пінів, коли картка
//               не монтується (лише SPI);
//   SD_READER - sdraw/sdext4/sdbench/sdcrc/sdverify/sdmap: сирі сектори в
//               обхід ФС (порятунок даних, бенчмарк, карта деградації);
//               sdbench/sdcrc/sdmap блокують loop() на десятки секунд -
//               перед ними 'watchdog off';
//   SD_IMAGE  - sdimg: уся картка по HTTP для зняття образу, дисплей на цей
//               час заморожений (лише SPI).
// 0 - картка лишається носієм: монтування, 'status sd'/'sd+', System-вкладка.
#define HAS_SD_WORKBENCH 1
// USB Mass Storage: команда 'sdmsc on' віддає картку хосту як read-only
// USB-накопичувач (TinyUSB MSC, src-<env>/SdMassStorage.cpp), без зняття
// картки зі слоту. Хост читає сектори через той самий ActiveBulkReader, що й
// SD_READER; після серії збоїв читання USB-стек просить перемонтувати картку,
// і робить це loop() (remountCardIfMscAsked()), а не таск TinyUSB - виклик
// end()/begin() драйвера звідти валив плату. Потрібен native USB і
// реалізація SdMassStorage.cpp у src-<env>/ - тому окремо від HAS_SD_WORKBENCH.
#define HAS_SD_MSC 1

// MQTT-клієнт (lib/MqttClient) - опис принципу в env:esp32-4848s040.
// PicoMQTT присутній у lib_deps цього env.
#define HAS_MQTT_CLIENT 1

// Дзеркало консолі в MQTT (lib/ConsoleMqtt) - опис принципу в env:esp32-4848s040.
#define HAS_CONSOLE_MQTT 1

// Chrome Dino на екрані: команда "dino on|off" (src/Dino/, lib/DinoGame).
// Тач тут відсутній - стрибок лише кнопкою PRIMARY_BUTTON_PIN (розділ 4).
#define HAS_DINO_GAME 1

// Веб-портал (lib/WebPortal) - опис механізму в env:esp32-c6. PSRAM 8 МБ і
// app-розділ 3 МБ, тому обмежень тут немає.
#define HAS_WEB_PORTAL 1

// Телеметрія EcoFlow (src/Ecoflow/) - опис в env:esp32-c6. Умова механізму
// (PicoMQTT) виконана; PSRAM знімає питання heap.
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

// Кільце журналу (lib/Journal): 48 записів x ~180 Б.
#define JOURNAL_RING 48
#define CONFIG_HEAP_POISONING_COMPREHENSIVE 1

// Черга команд (lib/CommandQueue) - спільний ліміт "скільки команд у
// польоті" для serial/MQTT SAPI/порталу/cron разом. Фактичний троттлінг
// SAPI - на боці браузера (sapi/index.html, SAPI_MAX_INFLIGHT=2), не тут;
// це число тримають БІЛЬШИМ за нього там, де вистачає RAM (з запасом на
// решту джерел - інакше SAPI сам вичерпував би всю чергу), а на
// найтісніших платах - рівним йому, свідомо. Повне пояснення й таблиця
// по всіх платах - docs/mqtt-web-handoff.md, "SAPI: обмеження одночасних
// запитів".
#define COMMAND_QUEUE_SLOTS 4

// ARDUINO_USB_MODE=0 (OTG/TinyUSB, потрібен для USB MSC) задається в
// platformio.ini через build_flags, не тут: цей заголовок підключається
// через -include РАНІШЕ, ніж board-манфест esp32-s3-devkitc-1 підставляє
// свій -DARDUINO_USB_MODE=1, тому #define тут мовчки перебивається назад.
#define ARDUINO_USB_CDC_ON_BOOT 1

// ============================================================
// 4. Піни та налаштування окремих сутностей/датчиків
// ============================================================

// ---------- Дисплей: ST7789 172x320 (пини - Setup_ST7789_lcd147.h) ----------
#define TFT_ROTATION 3  // 172x320
#define SPRITE_COLOR_DEPTH 16
// #define SPRITE_COLOR_DEPTH 8

// DISPLAY_SPLIT_COUNT - те саме ділення height() ПІСЛЯ ротації, що й на
// esp32-c6 (див. коментар там); тут 320 підходящих дільників не бракує.
#define DISPLAY_SPLIT_COUNT 2

// TOUCH_CS=0 - НЕ реальний тач (BOARD_HAS_TOUCHSCREEN=0 вище), лише
// придушення warning бібліотеки: .pio/libdeps/esp32-s3-lcd147/TFT_eSPI/
// TFT_eSPI.h:973 попереджає "TOUCH_CS pin not defined" навіть коли тач не
// використовується.
#define TOUCH_CS 0

// Фонове зображення - три взаємовиключні варіанти (повна матриця по всіх
// платах - docs/architecture.md, розділ "Матриця фіч по платах"):
//   1) BACKGROUND_IMAGES_COUNT >= 1 - вшита в прошивку картинка, 0 байт heap.
//   2) LITTLEFS_BACKGROUND_IMAGE="/файл.jpg" - декодується в RAM при старті.
//   3) BACKGROUND_PROGMEM_HEADER="шлях.h" - RGB565-масив запечений у Flash.
// На цій платі: варіант 2, PSRAM знімає питання heap.
#define BACKGROUND_IMAGES_COUNT 0
#define LITTLEFS_BACKGROUND_IMAGE "/background-01-320x172.jpg"

// ---------- TF-картка ----------
// TF-карта на роз'ємі підключена як SD_MMC (4-bit: D0/D1/D2/D3/CLK/CMD),
// піни з офіційного прикладу Waveshare/Espressif (ws-s3-lcd-1-47): D0=16,
// D1=18, D2=17, D3=21, CLK=14, CMD=15.
//
// SD_FORCE_SPI - читати картку по SPI, а не по SDMMC, на ТИХ САМИХ пінах.
// НАВІЩО: SD-картка розуміє обидва протоколи на одному роз'ємі, і для
// деградованої картки різниця виявилась вирішальною. SDMMC перевіряє CRC
// апаратно і після серії збоїв заводить картку в стан помилки, з якого її
// виводить лише зняття живлення (перевірено двічі). SPI-драйвер ті самі
// сектори читає годинами без залипань - саме так було знято всі дані на
// esp32-c6. Швидкість при цьому не страждає: вузьке місце - USB
// (752 KiB/s), а не інтерфейс картки (SPI дає 1.35 MiB/s).
//
// Відповідність пінів роз'єму TF-карти: CS=D3, SCK=CLK, MOSI=CMD, MISO=D0.
#define SD_FORCE_SPI 1
#define SD_CS 21
#define SD_SCK 14
#define SD_MOSI 15
#define SD_MISO 16
#define SD_FREQ 20000000
// Піни SDMMC лишаємо оголошеними: код SD_MMC-гілки далі компілюється, і
// повернутися до неї можна прибравши лише SD_FORCE_SPI.
#define SD_D0 16
#define SD_D1 18
#define SD_D2 17
#define SD_D3 21
#define SD_CLK 14
#define SD_CMD 15

// ---------- Кнопка ----------
#define PRIMARY_BUTTON_PIN 0

// ============================================================
// 5. Налаштування фіч
// ============================================================

// EcoFlow (HAS_ECOFLOW_CLIENT, розділ 2).
#define ECOFLOW_AUTOCONNECT 1
#define ECOFLOW_SYNC_ON_BOOT 1
