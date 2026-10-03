// environment.h
// Компіляційні налаштування плати esp32-c3 (TENSTAR ESP32-C3 SuperMini,
// БЕЗ дисплея). Порядок розділів і правило оформлення - CLAUDE.md,
// розділ "environment.h кожної плати".
//
// Підключається через build_flags у platformio.ini (env:esp32-c3):
//   -include src-esp32-c3/environment.h

#pragma once

// ============================================================
// 1. Ідентифікація плати
// ============================================================
#define BOARD_ESP32_C3 1
// Назва плати для MQTT discovery (поле "board", src/Mqtt/Discovery.cpp).
#define BOARD_NAME "esp32-c3"

// ============================================================
// 2. Можливості та фічі (BOARD_HAS_* та HAS_*)
// ============================================================

// ГОЛОВНЕ для цієї плати: дисплея немає взагалі. BOARD_HAS_DISPLAY=0
// перемикає lib/Display/DisplayDriver.hpp на заглушку include/Setup_Headless.h -
// клас з API TFT_eSPI, у якого всі методи порожні й inline. Прикладний
// код (src/main.cpp, lib/Display/*, src/ntp.h, src/setup.h) лишається
// спільним з іншими платами, але компілятор викидає з нього весь вивід
// на екран.
#define BOARD_HAS_DISPLAY 0
#define BOARD_HAS_TOUCHSCREEN 0
#define BOARD_HAS_SD 0

// MQTT-клієнт (lib/MqttClient) - опис принципу в env:esp32-4848s040.
// PicoMQTT присутній у lib_deps цього env (єдина плата без коментованого
// PubSubClient - тут лише один рядок mlesniew/PicoMQTT).
#define HAS_MQTT_CLIENT 1

// Дзеркало консолі в MQTT (lib/ConsoleMqtt) - опис принципу в env:esp32-4848s040.
#define HAS_CONSOLE_MQTT 1

// Без дисплея гра неможлива в принципі.
#define HAS_DINO_GAME 0

// Веб-портал (lib/WebPortal): та сама вебка, що на обох C6 - розділи
// Wi-Fi і Console, доступна і в домашній мережі, і на AP-fallback точці.
// Дисплея тут немає (BOARD_HAS_DISPLAY=0), тому вебка - ЄДИНИЙ спосіб
// подивитись на пристрій, не тримаючи його на кабелі. Запас у 2 МБ
// app-розділі це дозволяє з великим відривом. Статика: LittleFS /www
// ('pio run -t uploadfs'), інакше вшита копія (tools/gen_web_assets.py).
// Логін/пароль - NVS, ключі web_user/web_pass.
#define HAS_WEB_PORTAL 1

// EcoFlow. Вмикається ЯВНО (як і на C6), а не виводиться з
// __has_include(<PicoMQTT.h>): той ланцюжок правильно рахує лише
// компілятор, IDE-індексатор часто не бачить .pio/ і гасить весь блок.
//
// Портал + EcoFlow тут НЕ вміщаються разом із прямим TLS до хмари
// EcoFlow так само, як на ttgo-t1/esp32-st7789 (docs/tech_debt.md §4) -
// відсутність спрайта/фонового буфера від цього не рятує, головні
// витрати ті самі (AsyncTCP-буфери порталу + ~57 КБ TLS-сесія EcoFlow).
// HTTP_SERVER_MAX_CLIENTS=2 як спроба лікування СПРОБУВАНО й ВІДКОЧЕНО
// (docs/tech_debt.md §4) - сторінка сама тримає SSE-потік консолі
// відкритим постійно плюс 4 паралельні fetch на завантаженні, тож 2
// клієнти замало навіть для однієї вкладки. maxClients лишається
// бібліотечним дефолтом (4). Замість прямого TLS - MQTT-проксі (розділ 5).
#define HAS_ECOFLOW_CLIENT 1

// Пінг (src/ping.h) вимкнено - dvarrel/ESPping відсутній у lib_deps цього
// env. Опис принципу в env:esp32-4848s040.
#define HAS_PING 0

// Gmail (lib/GmailSender) - опис принципу в env:esp32-4848s040.
// mobizt/ReadyMail присутній у lib_deps цього env.
#define HAS_GMAIL_SENDER 1

// ============================================================
// 3. Системні піни та налаштування
// ============================================================

// Кільце журналу (lib/Journal): 16 записів x ~180 Б - менше, ніж на C6
// (32), бо тут лише 320 КБ RAM, а портал+EcoFlow обов'язкові й без
// дисплея-запасного шляху (див. lib/Journal/Journal.hpp).
#define JOURNAL_RING 16
#define CORE_DEBUG_LEVEL 2

// Черга команд (lib/CommandQueue) - спільний ліміт "скільки команд у
// польоті" для serial/MQTT SAPI/порталу/cron разом. Фактичний троттлінг
// SAPI - на боці браузера (sapi/index.html, SAPI_MAX_INFLIGHT=2), не тут;
// це число тримають БІЛЬШИМ за нього там, де вистачає RAM (з запасом на
// решту джерел - інакше SAPI сам вичерпував би всю чергу), а на
// найтісніших платах - рівним йому, свідомо. Повне пояснення й таблиця
// по всіх платах - docs/mqtt-web-handoff.md, "SAPI: обмеження одночасних
// запитів".
#define COMMAND_QUEUE_SLOTS 4

#define ARDUINO_USB_MODE 1
#define ARDUINO_USB_CDC_ON_BOOT 1

// ============================================================
// 4. Піни та налаштування окремих сутностей/датчиків
// ============================================================

// ---------- Дисплей (відсутній, BOARD_HAS_DISPLAY=0) ----------
// Розміри лишаються визначеними як 0, а не викинутими: main.cpp друкує
// їх у діагностиці ("Display: %dx%d") поза будь-якими #if. Разом з
// дисплеєм вимкнено все, що від нього залежить:
//   DISPLAY_SPLIT_COUNT=0    - без спрайта/буфера кадру (lib/Display/TftEspiDriver.hpp
//                              бере гілку "sprite() == tft_")
//   BACKGROUND_IMAGES_COUNT=0 + без LITTLEFS_BACKGROUND_IMAGE
//                            - жодних фонових JPEG у RAM
//   DISPLAY_BUS_YIELD не задано - SPI-шини дисплея немає, ділити нема з чим
//   CLOCK_*/DATE_* не задані - блоки годинника й дати в main.cpp стоять
//                              під "#if CLOCK_TEXT_FONT && ...", незаданий
//                              макрос у #if дає 0
//   LOAD_FONT*/U8G2_FONT_SUPPORT не задані - шрифти нікуди виводити
// TFT_CS/TFT_DC/TFT_RST/TFT_BL теж НЕ визначені: main.cpp і TftEspiDriver.cpp
// перевіряють їх через "#if defined(...)", тож робота з пінами дисплея
// (підсвітка, резервування пінів у gpio-командах) сама зникає зі збірки.
#define TFT_WIDTH 0
#define TFT_HEIGHT 0
#define TFT_ROTATION 0
#define SPRITE_COLOR_DEPTH 16
#define DISPLAY_SPLIT_COUNT 0
#define BACKGROUND_IMAGES_COUNT 0

// ---------- LED ----------
// Вбудований LED на GPIO8, інверсна логіка (LOW = світиться) - саме так
// його й вмикає main.cpp. Це ЄДИНИЙ засіб індикації на платі без екрана,
// тому лишається ввімкненим завжди.
//
// PRIMARY_BUTTON_PIN (BOOT на GPIO9) свідомо НЕ задано: уся його логіка в
// main.cpp суто дисплейна - flip орієнтації, яскравість підсвітки,
// показ/приховування годинника. Без екрана це була б cron-задача, що
// щотика читає GPIO заради no-op.
#define BLINK_LED_PIN 8

// ============================================================
// 5. Налаштування фіч
// ============================================================

// EcoFlow (HAS_ECOFLOW_CLIENT, розділ 2). Сесія коштує ~57 КБ heap
// (mbedTLS-буфери). Runtime-override: ConfigStorage ключ 'ecoflow.auto'/
// 'ecoflow.sync' (команди 'ecoflow-auto on|off' / 'ecoflow-sync on|off').
#define ECOFLOW_AUTOCONNECT 1
#define ECOFLOW_SYNC_ON_BOOT 1
// ECOFLOW_MQTT_PROXY_HOST/USERNAME/PASSWORD лишаються в platformio.ini:
// усі три беруться з secrets.ini (${secrets.*}) - заголовок не бачить
// змінних PlatformIO. MQTT-проксі (docs/tech_debt.md, "MQTT-проксі: винести
// TLS з ESP32 на зовнішній хост", стадія 1 ПІДТВЕРДЖЕНА 18.09.2026):
// портал+EcoFlow разом не влазять у heap цієї плати з прямим TLS до хмари
// EcoFlow (~57 КБ на mbedTLS). Замість нього - plain MQTT до
// mosquitto-проксі на rpi5, який сам тримає одну TLS-сесію на весь LAN
// (EcoflowClient::Config::proxyHost).
