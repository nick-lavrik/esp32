// environment.h
// Компіляційні налаштування плати esp32-c6 (Waveshare ESP32-C6-Touch-LCD-1.47):
// піни, можливості (BOARD_HAS_*) і фічі (HAS_*) - усе, що раніше жило як -D
// у build_flags цього env. Порядок розділів і правило оформлення - CLAUDE.md,
// розділ "environment.h кожної плати".
//
// Підключається через build_flags у platformio.ini (env:esp32-c6):
//   -include src-esp32-c6/environment.h
// -include діє на кожен TU ще до першого рядка його джерела (як і -D), тож
// зміна ЦЬОГО файлу не чіпає сигнатуру збірки жодного іншого env.

#pragma once

// ============================================================
// 1. Ідентифікація плати
// ============================================================
#define BOARD_ESP32_C6 1

// ============================================================
// 2. Можливості та фічі (BOARD_HAS_* та HAS_*)
//    Що плата має "по залізу" і що з цього ввімкнено програмно - весь
//    маніфест тут, без пінів і деталей. Налаштування фіч, яким цього
//    самого on/off не досить (ecoflow) - розділ 5, після пінів.
// ============================================================
#define BOARD_HAS_DISPLAY 1
#define BOARD_HAS_TOUCHSCREEN 1
#define BOARD_HAS_IMU 1
#define BOARD_HAS_SD 1

// MQTT-клієнт (lib/MqttClient) - опис принципу в env:esp32-4848s040.
// PicoMQTT присутній у lib_deps цього env.
#define HAS_MQTT_CLIENT 1

#define HAS_DINO_GAME 1

// Веб-портал (lib/WebPortal): розділи Wi-Fi і Console, доступна і в
// домашній мережі, і на AP-fallback точці ("ESP-<env>"). Статика береться
// з LittleFS (/www, кладеться 'pio run -t uploadfs'), а якщо розділ
// порожній - з вшитої в прошивку копії (tools/gen_web_assets.py).
// Логін/пароль (HTTP Basic) лежать у NVS, ключі web_user / web_pass; поки
// пароль не заданий, портал відкритий.
#define HAS_WEB_PORTAL 1

// EcoFlow Open Platform. Вмикається ЯВНО тут, а не виводиться в main.cpp з
// "__has_include(<PicoMQTT.h>) && defined(ECOFLOW_MQTT_HOST)": той ланцюжок
// правильно рахує лише компілятор. IDE-індексатори часто виключають .pio/
// з індексації, там __has_include(<PicoMQTT.h>) дає false - і весь блок
// EcoFlow у редакторі стає "мертвим", хоча збірка його включає.
//
// Прив'язано до env з PicoMQTT навмисно: suspend()/resume() (пауза TLS на
// час REST) реалізовані лише в PicoMQTT-гілці MqttClient, у PubSubClient-
// гілці вони no-op, і REST падав би по heap.
#define HAS_ECOFLOW_CLIENT 1

// Пінг (src/ping.h) вимкнено - dvarrel/ESPping закоментовано в lib_deps
// цього env. Опис принципу в env:esp32-4848s040.
#define HAS_PING 0

// Gmail (lib/GmailSender) - опис принципу в env:esp32-4848s040.
// mobizt/ReadyMail присутній у lib_deps цього env.
#define HAS_GMAIL_SENDER 1

// ============================================================
// 3. Системні піни та налаштування
//    (не належать жодному окремому датчику - шини, USB, діагностика)
// ============================================================

// Кільце журналу (lib/Journal): 32 записів x ~180 Б.
#define JOURNAL_RING 32
#define CORE_DEBUG_LEVEL 2

#define ARDUINO_USB_MODE 1
#define ARDUINO_USB_CDC_ON_BOOT 1

// I2C-шина СПІЛЬНА для тача AXS5106L і IMU QMI8658A (обидва - розділ 4).
// Джерело пінів - docs.waveshare.com/ESP32-C6-Touch-LCD-1.47 (SDA/SCL/TP_RST
// підтверджені ще й незалежно, github.com/toto04/axs5106l).
#define I2C_SDA 18
#define I2C_SCL 19

// SPI-шина дисплея СПІЛЬНА з TF-карткою (SD_SCK=1/SD_MOSI=2 = TFT_SCLK/
// TFT_MOSI з include/Setup_JD9853_C6.h, окремі лише MISO/CS - розділ 4).
// Без цього прапорця будь-яке звернення до картки з консольної чи
// MQTT-команди вішає плату - деталі в docs/architecture.md, виноска ².
//
// УВАГА: прапорець ширший, ніж каже його історія (назва - про SD, але
// transaction-лок бере і сам Arduino_GFX, напр. Arduino_ST7735::
// setRotation() у display_flip()) - без нього команда flip з консолі
// вішала плату намертво, без шансу на watchdog, бо задача коректно
// блокується на семафорі. Тримати треба заради ОБОХ сторін, не лише SD.
#define DISPLAY_BUS_YIELD 1

// ============================================================
// 4. Піни та налаштування окремих сутностей/датчиків
// ============================================================

// ---------- Дисплей: JD9853 172x320 ----------
// Піни SPI самого дисплея (SCLK/MOSI/CS/DC/RST/BL) - в
// include/Setup_JD9853_C6.h (там і клас-фасад над Arduino_GFX). TFT_WIDTH/
// TFT_HEIGHT задані і тут, і там: main.cpp читає їх напряму (діагностика,
// розрахунок координат) до того, як гарантовано підключиться той заголовок,
// а -include (як і -D) діє на кожен TU однаково рано - дублювання нешкідливе,
// бо значення завжди співпадають.
#define TFT_WIDTH 172
#define TFT_HEIGHT 320

// landscape, як в офіційній документації плати - з правильною
// init-послідовністю (jd9853_reg_init_operations) 1 працює чисто, "5" був
// компенсацією за generic ST7789 init.
#define TFT_ROTATION 3

// SPRITE_COLOR_DEPTH керує ГЛИБИНОЮ ФОНОВОГО ЗОБРАЖЕННЯ (див. вибір
// JpegColorDepth у setupBackgroundImage()), сам sprite від нього не залежить.
// 8 -> RGB332: фон 320x172 займає 55 КБ замість 110 КБ - найбільший
// одиничний споживач heap на цій платі, а вільно було лише 13%.
// Display::pushImage8bpp() конвертує RGB332->RGB565 рядок за рядком, тому
// на якість виводу це не впливає - лише на градації кольору самого фону.
#define SPRITE_COLOR_DEPTH 16
// #define SPRITE_COLOR_DEPTH 8

// DISPLAY_SPLIT_COUNT ділить height() ПІСЛЯ ротації, а не фізичну висоту
// панелі. В landscape-орієнтації (TFT_ROTATION=1) height() повертає 172, не
// 320 (src/Display.h ротує _width/_height місцями). 172 = 4 * 43 - ділиться
// без залишку тільки на 1/2/4/43/86/172. Був DISPLAY_SPLIT_COUNT=8 ->
// 172/8=21.5 -> 4 рядки знизу не потрапляли в жоден спліт -> смуга сміття.
#define DISPLAY_SPLIT_COUNT 4

// Шрифти - через шар сумісності include/ArduinoGfxFonts.h (тут Arduino_GFX,
// а не TFT_eSPI). Екран 320x172 значно більший за lcd096, тому доступні всі
// реалізовані номери. FONT6/FONT8 не реалізовані (див. ArduinoGfxFonts.h).
#define LOAD_GLCD 1
#define LOAD_FONT2 1
#define LOAD_FONT4 1
#define LOAD_FONT7 1
// U8G2_FONT_SUPPORT вмикає в Arduino_GFX перевантаження setFont(const
// uint8_t*) + setUTF8Print() - без них 7-сегментний шрифт (FONT7)
// підключити нічим. Сама бібліотека U8g2 НЕ потрібна: Arduino_GFX має
// власний декодер u8g2-шрифтів і в U8g2 не звертається; прапорець у
// заголовку виставляється через "#if __has_include(<U8g2lib.h>)", задати
// його напряму - коректний і дешевший спосіб, ніж тягнути залежність
// заради одного масиву. Побічний плюс: CJK-шрифти з того ж блоку в
// прошивку не потрапляють.
#define U8G2_FONT_SUPPORT 1

// Годинник і дата на екрані.
#define CLOCK_TEXT_FONT 1
#define CLOCK_TEXT_SIZE 5
// CLOCK_POS_X не задано - auto
#define CLOCK_POS_Y 45
#define DATE_TEXT_FONT 4
#define DATE_TEXT_SIZE 1
// DATE_POS_X не задано - auto
#define DATE_POS_Y 110

// Фонове зображення - три взаємовиключні варіанти (повна матриця по всіх
// платах - docs/architecture.md, розділ "Матриця фіч по платах", рядки
// "Фон, запечений у Flash" / "Фон із LittleFS" / "Вбудовані фони"):
//   1) BACKGROUND_IMAGES_COUNT >= 1, без двох прапорців нижче - картинка
//      вшита в прошивку як масив (src/BackgroundImages.cpp), 0 байт heap.
//   2) LITTLEFS_BACKGROUND_IMAGE="/файл.jpg" - декодується в RAM при
//      старті (setupBackgroundImage()), коштує width*height*2 байт heap.
//   3) BACKGROUND_PROGMEM_HEADER="шлях.h" - готовий RGB565-масив запечений
//      у Flash (генерується з живого пристрою, з накладеними ефектами -
//      команда "./esp bg-save", 0 байт RAM. Обрано тут.
//
// На цій платі: BACKGROUND_IMAGES_COUNT=0 (варіант 1 вимкнено) і
// LITTLEFS_BACKGROUND_IMAGE не задано (варіант 2 вимкнено) - фон запечений
// у Flash (варіант 3), бо вільного heap лишалось лише 13%.
#define BACKGROUND_IMAGES_COUNT 0
// #define LITTLEFS_BACKGROUND_IMAGE "/background-02-320x172.jpg"
//
// Хедер генерується з ЖИВОГО пристрою, разом із накладеними ефектами
// (на відміну від data/convert.c, який бере оригінальний jpeg БЕЗ них):
//   1) зібрати як є (LITTLEFS_BACKGROUND_IMAGE + SPRITE_COLOR_DEPTH=16)
//   2) підібрати вигляд командами: blur / desaturate / tint / darken
//   3) ./esp bg-save assets/background-02-320x172.h
//   4) розкомментувати LITTLEFS_BACKGROUND_IMAGE вище -> закомментувати
//      рядок нижче -> повернутись до варіанту 2, якщо потрібно перегенерувати
#define BACKGROUND_PROGMEM_HEADER "../assets/background-02-320x172.h"

// ---------- Тач AXS5106L ----------
// Адреса ПІДТВЕРДЖЕНА на залізі командою "i2cscan": 0x63. (Waveshare FAQ
// називає 0x51 - це неправда для цієї плати; правий виявився драйвер
// toto04/axs5106l. Fallback на 0x51 у драйвері лишено на випадок іншої
// ревізії плати, але в нормі він не спрацьовує.)
#define TOUCH_AXS5106L 1
#define TOUCH_INT 21
#define TOUCH_RST 20

// ---------- IMU QMI8658A ----------
// Акселерометр+гіроскоп, адреса підтверджена "i2cscan": 0x6B. Використовується
// лише акселерометр: поворот плати на 180 градусів У ПЛОЩИНІ ЕКРАНА (верхній
// рядок стає нижнім) перемикає орієнтацію зображення через display_flip().
//
// IMU_UP_AXIS=0 (X) - вісь, що змінює знак саме при ТАКОМУ повороті;
// перевірено на залізі. Осі Z для цього НЕ годиться: вона реагує на
// перевертання екраном донизу, тобто на зовсім інший рух. IMU_UP_AXIS_SIGN
// впливає лише на підписи TopUp/TopDown у логах - сам flip реагує на зміну
// орієнтації відносно стартової.
#define IMU_QMI8658 1
#define IMU_UP_AXIS 0
#define IMU_UP_AXIS_SIGN -1
#define IMU_INT1 5
#define IMU_INT2 6

// ---------- TF-картка ----------
// esp32-c6 - CS=4, SCK=1, MISO=3, MOSI=2 (docs.waveshare.com/
// ESP32-C6-Touch-LCD-1.47). Шина SCK/MOSI спільна з дисплеєм - див.
// DISPLAY_BUS_YIELD у розділі 3.
#define SD_CS 4
#define SD_MOSI 2
#define SD_MISO 3
#define SD_SCK 1
#define SD_FREQ 40000000

// ---------- Кнопка ----------
// BOOT button (docs.waveshare.com/ESP32-C6-Touch-LCD-1.47)
#define FLIP_BUTTON_PIN 9

// ============================================================
// 5. Налаштування фіч
//    (сама увімкненість HAS_* - розділ 2; тут лише те, що потребує
//    ще й КОНФІГУРАЦІЇ понад просте on/off)
// ============================================================

// EcoFlow (HAS_ECOFLOW_CLIENT, розділ 2).
// 1 - піднімати EcoFlow-MQTT одразу на старті; 0 - лише за командою
// 'ecoflow-start'. Сесія коштує ~57 КБ heap (mbedTLS-буфери), тому на платі
// без PSRAM це відчутний вибір. Runtime-override: ConfigStorage ключ
// 'ecoflow.auto' (команда 'ecoflow-auto on|off').
#define ECOFLOW_AUTOCONNECT 1
// ключ 'ecoflow.sync' (команда 'ecoflow-sync on|off').
#define ECOFLOW_SYNC_ON_BOOT 0
// ECOFLOW_MQTT_PROXY_HOST/USERNAME/PASSWORD лишаються в platformio.ini:
// усі три беруться з secrets.ini (${secrets.*}) - заголовок не бачить
// змінних PlatformIO, а розносити один логічний набір по двох файлах
// було б гірше, ніж тримати всі три разом.

// Відлагодження PicoMQTT (за потреби розкомментувати):
// #define PICOMQTT_DEBUG
// #define PICOMQTT_DEBUG_TRACE_FUNCTIONS
// CONFIG_LWIP_TCP_MSS=536 не задається навмисно: sdkconfig.h фреймворку вже
// визначає його як 1436, і ручне перевизначення лише дає компілятор-
// warning "redefined" без жодного ефекту.
