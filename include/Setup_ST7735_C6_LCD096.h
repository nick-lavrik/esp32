// Setup_ST7735_C6_LCD096.h
// TFT_eSPI-сумісний шар над moononournation/Arduino_GFX для ST7735S (160x80).
//
// Плата: "ESP32-C6-LCD-0.96" (ESP32-C6FH4, ST7735S SPI, 160x80, без PSRAM)
// https://www.aliexpress.com/item/1005012568785967.html
//
// ПРИЧИНА (файл): bodmer/TFT_eSPI і lovyan03/LovyanGFX (вже використовуються
// в проєкті) не тестувались на ESP32-C6 (RISC-V) у цьому проєкті — для
// іншої C6-плати (esp32-c6, JD9853) вже обрано Arduino_GFX з тих самих
// причин (див. Setup_JD9853_C6.h), тому тут — той самий підхід, заради
// консистентності.
//
// !! НАЗВА ЧІПА !!: назва товару на AliExpress каже "ST7789", але це
// ОМАНЛИВО. І схема плати (роз'єм дисплея підписаний "0.96 inch LCD
// ST7735"), і тестова init-послідовність, надана Миколою (регістри
// 0xB1/0xB2/0xB3 - frame rate control, 0xC0-0xC5 - power control,
// 0xE0/0xE1 - gamma) — це КЛАСИЧНИЙ регістровий набір ST7735(S), не
// ST7789 (у ST7789 зовсім інші коди: 0xB2=PORCTRL, 0xB7=GCTRL,
// 0xD0=PWCTRL1 тощо). Тому тут використовується Arduino_ST7735, а не
// Arduino_ST7789.
//
// Init-послідовність нижче — дослівний перенос перевіреної на реальному
// пристрої послідовності (наданий Миколою тестовий приклад,
// LCD_Init()/EXAMPLE_PIN_NUM_*), команда-в-команду, БЕЗ спроби замінити
// generic Arduino_GFX ST7735-init — той самий принцип, що й у
// Setup_JD9853_C6.h ("довіряти перевіреній на залізі послідовності
// більше, ніж generic init бібліотеки").
//
// Офсети CASET/RASET з тестового коду (Offset_X/Offset_Y): 160+2*1=162 і
// 80+2*26=132 точно відповідають нативному GRAM ST7735 (132x162) — додаткове
// підтвердження, що чіп справді ST7735, а не ST7789 (у якого GRAM 240x320).
//
// УВАГА, розбіжність осей: TFT_WIDTH/TFT_HEIGHT нижче задані як 80x160
// (НАТИВНА портретна орієнтація панелі), а 160x80 отримується вже через
// TFT_ROTATION=1. Тому для нативних осей офсети — col=26 (132-80=52 -> 26+26),
// row=1 (162-160=2 -> 1+1), саме в такому порядку вони й передані в
// конструктор TFT_eSPI нижче.
//
// SPI-шина - ЄДИНА, СПІЛЬНА з TF-карткою (Arduino_HWSPI їде на глобальному
// об'єкті `SPI`, не створює власний хост — той самий підхід, що й
// Setup_JD9853_C6.h). `SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS)`
// викликається в src/main.cpp::setupSD() — обов'язково ПЕРЕД
// setupDisplay().
//
// НАСЛІДОК-ПАСТКА №1: оскільки setupSD() іде ПЕРШИМ, на момент його роботи
// TFT_CS ще не сконфігурований і висить плаваючим входом — дисплей приймає
// весь init-трафік картки за власні команди. setupSD() тому явно піднімає
// TFT_CS у HIGH. Не прибирати.
//
// НАСЛІДОК-ПАСТКА №2: звертатися до ШИНИ з-під ВІДКРИТОЇ транзакції
// дисплея не можна — SPIClass::beginTransaction() бере не рекурсивний
// мьютекс із portMAX_DELAY, і плата зависає намертво (watchdog не
// допоможе: задача коректно блокується на семафорі). А loop() тримає цю
// транзакцію на весь кадр, включно з commandHandler.update()/mqtt.loop().
//
// Стосується не лише SD: транзакцію бере і сам Arduino_GFX — наприклад
// Arduino_ST7735::setRotation(), тобто звичайний flip екрана з консолі.
// Для цього в src/main.cpp є YIELD_DISPLAY_BUS() під прапорцем
// DISPLAY_BUS_YIELD=1 — будь-який НОВИЙ код, що чіпає шину з команди,
// має бути під цією дужкою. Детальніше — docs/architecture.md, виноска ².
//
// Піни (зі схеми плати, підтверджені тестовим кодом):
// SCK=7, MOSI=6, MISO=5 (лише SD — дисплей write-only), CS: 4 (SD) / 14
// (LCD), DC=15, RST=21, підсвітка (через транзисторний ключ) = GPIO3.
//
// Підсвітку (analogWrite(TFT_BL,...)) і pinMode(TFT_BL, OUTPUT) робить
// спільний код (src/Display.cpp, src/setup.h) — тут лише #define TFT_BL,
// власного PWM-коду не потрібно.
//
// TFT_ROTATION / фінальна орієнтація — НЕ перевірені на реальному
// пристрої, потребують валідації (аналогічно іншим платам проєкту).

#pragma once

#include <Arduino.h>
#include <Arduino_GFX_Library.h>

#include <Logger.hpp>

#include "ArduinoGfxFonts.h"

// ---------- Піни SPI дисплея (ST7735S) ----------
// Джерело: схема плати + тестовий код (Display_ST7789.h від Миколи,
// EXAMPLE_PIN_NUM_*) — піни збігаються з обох джерел.
// #define TFT_WIDTH 160
// #define TFT_HEIGHT 80
#define TFT_SCLK 7
#define TFT_MOSI 6
#define TFT_MISO -1  // дисплей не має read-back лінії (write-only); SD MISO=5 — окремо
#define TFT_CS 14
#define TFT_DC 15
#define TFT_RST 21
#define TFT_BL 3
#define TFT_BACKLIGHT_ON HIGH

// ---------- Кольори (RGB565) — стандартні значення TFT_eSPI/Adafruit_GFX.
#define TFT_BLACK 0x0000
#define TFT_WHITE 0xFFFF
#define TFT_RED 0xF800
#define TFT_GREEN 0x07E0
#define TFT_BLUE 0x001F
#define TFT_DARKGREEN 0x03E0
#define TFT_YELLOW 0xFFE0
#define TFT_CYAN 0x07FF
#define TFT_MAGENTA 0xF81F
#define TFT_ORANGE 0xFDA0
#define TFT_DARKGREY 0x7BEF
#define TFT_LIGHTGREY 0xC618

// Датуми тексту (підмножина TFT_eSPI, якої вистачає src/Display.cpp)
#define TL_DATUM 0
#define MC_DATUM 4

// ---------- Шрифти ----------
// #define LOAD_GLCD
// #define LOAD_FONT2
// #define LOAD_FONT4
// #define LOAD_FONT6
// #define LOAD_FONT7
// #define LOAD_FONT8

// Дослівний перенос перевіреної на реальному пристрої init-послідовності
// ST7735S (наданий Миколою тестовий приклад, LCD_Init()). Викликається з
// init() ПІСЛЯ begin() (яке лишень відкриває SPI-шину і шле generic
// ST7735-init бібліотеки, не критично що саме) і ПЕРЕД setRotation()
// (0x36 тут навмисно = 0xA8, тимчасовий - фінальний MADCTL все одно
// перезаписує бібліотечний Arduino_ST7735::setRotation(), викликаний з
// src/Display.cpp одразу після init()).
//
// НЕ перенесено з оригіналу: початкове CASET(0x2A)/RASET(0x2B) вікно
// (0,0)-(159,79) з окремими хардкод-офсетами (+2/+3) одразу після
// gamma-регістрів — це transient/redundant крок, який в оригіналі однаково
// перезаписується першим реальним LCD_SetCursor() виклику; тут адресне
// вікно виставляє сама бібліотека Arduino_GFX при кожному draw-виклику,
// з офсетами (1, 26), заданими в конструкторі TFT_eSPI нижче.
static const uint8_t st7735_reg_init_operations[] = {
    BEGIN_WRITE,
    WRITE_COMMAND_8, 0x11,  // Sleep Out
    END_WRITE,
    DELAY, 120,

    BEGIN_WRITE,
    WRITE_C8_D8, 0x36, 0xA8,  // Memory Data Access Control - тимчасовий, див. коментар вище

    WRITE_COMMAND_8, 0xB1,  // Frame Rate Control (Normal mode)
    WRITE_BYTES, 3, 0x01, 0x2C, 0x2D,
    WRITE_COMMAND_8, 0xB2,  // Frame Rate Control (Idle mode)
    WRITE_BYTES, 3, 0x01, 0x2C, 0x2D,
    WRITE_COMMAND_8, 0xB3,  // Frame Rate Control (Partial mode)
    WRITE_BYTES, 6, 0x01, 0x2C, 0x2D, 0x01, 0x2C, 0x2D,

    WRITE_C8_D8, 0xB4, 0x07,  // Display Inversion Control (column inversion)

    WRITE_COMMAND_8, 0x21,  // Display Inversion On

    WRITE_COMMAND_8, 0xC0,  // Power Control 1
    WRITE_BYTES, 3, 0xA2, 0x02, 0x84,
    WRITE_C8_D8, 0xC1, 0xC5,  // Power Control 2
    WRITE_COMMAND_8, 0xC2,   // Power Control 3 (Normal mode, full colors)
    WRITE_BYTES, 2, 0x0A, 0x00,
    WRITE_COMMAND_8, 0xC3,  // Power Control 4 (Idle mode, 8 colors)
    WRITE_BYTES, 2, 0x8A, 0x2A,
    WRITE_COMMAND_8, 0xC4,  // Power Control 5 (Partial mode + full colors)
    WRITE_BYTES, 2, 0x8A, 0xEE,
    WRITE_C8_D8, 0xC5, 0x0E,  // VCOM Control 1

    WRITE_COMMAND_8, 0xE0,  // Positive Gamma Correction
    WRITE_BYTES, 16, 0x0F, 0x1A, 0x0F, 0x18, 0x2F, 0x28, 0x20, 0x22, 0x1F, 0x1B, 0x23, 0x37,
    0x00, 0x07, 0x02, 0x10,
    WRITE_COMMAND_8, 0xE1,  // Negative Gamma Correction
    WRITE_BYTES, 16, 0x0F, 0x1B, 0x0F, 0x17, 0x33, 0x2C, 0x29, 0x2E, 0x30, 0x30, 0x39, 0x3F,
    0x00, 0x07, 0x03, 0x10,

    WRITE_C8_D8, 0xF0, 0x01,  // Enable test command
    WRITE_C8_D8, 0xF6, 0x00,  // Disable RAM power save mode

    WRITE_C8_D8, 0x3A, 0x05,  // Interface Pixel Format - 16bit/pixel (RGB565)

    WRITE_COMMAND_8, 0x29,  // Display On
    END_WRITE};

// "Пристрій" - фізичний ST7735S дисплей. Публічний API, яким користується
// src/Display.h/.cpp (init/setRotation/getRotation/width/height/
// startWrite/endWrite/fillScreen/setCursor/print/...), успадкований від
// Arduino_ST7735/Arduino_GFX без змін; тут додається лише те, чого немає.
//
// Офсети: нативний GRAM ST7735 - 132x162; видима область панелі (нативно
// 80x160) центрована в ньому симетрично по обох осях -> col=26 (132-80=52),
// row=1 (162-160=2).
//
// ВІДКРИТЕ ПИТАННЯ (симптом "білі смуги зліва/справа" - недоторкані нашим
// малюванням залишки заводської прошивки): offset1 і offset2 тут ОДНАКОВІ,
// (26, 1). Але Arduino_GFX застосовує offset1 до rotation 0/2, а offset2 - до
// rotation 1/3, причому вже до ПОВЕРНУТИХ осей. Ця плата працює саме з
// TFT_ROTATION=1, тобто на широкій (160 px) осі використовується col-офсет 26
// замість 1 - що якраз і давало б зсув видимого вікна в GRAM з недоторканими
// краями. Наступний крок діагностики: спробувати offset2 = (1, 26).
// Перевірка потребує реального пристрою, тому код лишено як є.
class TFT_eSPI : public Arduino_ST7735 {
 public:
  TFT_eSPI()
      : Arduino_ST7735(_bus = new Arduino_HWSPI(TFT_DC, TFT_CS, TFT_SCLK, TFT_MOSI), TFT_RST,
                        0 /* rotation - виставляється окремо через setRotation() */,
                        true /* IPS - панель IPS, init-послідовність вище явно шле 0x21 (INVON) */,
                        TFT_WIDTH, TFT_HEIGHT
                        , 26 /* col offset1 */, 1 /* row offset1 */,  // portrait (rotation 0/2)
                          26 /* col offset2 */, 1 /* row offset2 */   // landscape (rotation 1/3)
                      ) {}

  void init() {
    if (!begin()) {
      Logger::error("[TFT_eSPI/ST7735] begin() failed - перевір піни/проводку SPI");
    }
    // Справжня, апаратно перевірена init-послідовність ST7735S (див.
    // коментар вище) - ЗАМІНЮЄ generic ST7735 init, який begin() щойно
    // надіслав.
    _bus->batchOperation(st7735_reg_init_operations, sizeof(st7735_reg_init_operations));

#if defined(TFT_BL)
    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, TFT_BACKLIGHT_ON);
#endif
  }

  // Arduino_GFX сам керує порядком байтів при передачі RGB565 по SPI -
  // no-op, лишений лише для сумісності сигнатури з src/Display.h.
  void setSwapBytes(bool) {}

  // Висота рядка ПОТОЧНОГО шрифту (див. ArduinoGfxFonts.h). Хардкод 8
  // тут був справедливий лише поки єдиним шрифтом був вбудований 5x7:
  // src/main.cpp рахує позиції рядків як row * (space + fontHeight()),
  // тож зі шрифтом 29 px і відповіддю 8 весь текст злипався б у кашу.
  size_t fontHeight() { return _fontHeight; }

  // Вибір шрифту за номером TFT_eSPI (1/2/4/7 - залежно від LOAD_FONT*).
  void setTextFont(uint8_t font) { _fontHeight = ArduinoGfxFonts::apply(this, font); }

  int16_t textWidth(const char *t) {
    int16_t x1, y1;
    uint16_t w, h;
    getTextBounds(t, 0, 0, &x1, &y1, &w, &h);
    return static_cast<int16_t>(w);
  }

  void drawString(const char *t, int32_t x, int32_t y) {
    setCursor(x, y);
    print(t);
  }

 private:
  uint8_t _fontHeight = 8;  // вбудований 5x7; оновлюється в setTextFont()

  // Зберігаємо власний вказівник на bus (той самий об'єкт, що передається
  // в Arduino_ST7735), бо базовий _bus у бібліотеці protected/недоступний
  // напряму для batchOperation() поза родиною Arduino_TFT.
  //
  // НАВМИСНО БЕЗ "= nullptr" тут! Присвоєння _bus = new Arduino_HWSPI(...)
  // в списку аргументів базового конструктора (вище) виконується ПІД ЧАС
  // обчислення цих аргументів - до того, як власна фаза ініціалізації
  // членів похідного класу взагалі починається. Default member
  // initializer тут затер би щойно присвоєне значення ПІСЛЯ виклику
  // базового конструктора (той самий баг, що описаний в
  // Setup_JD9853_C6.h).
  Arduino_DataBus *_bus;
};

/**
 * єдиний глобальний екземпляр, визначений в
 * @see file://./../src-esp32-c6-lcd096/TftInstance.cpp
 */
extern TFT_eSPI tft;

// "Спрайт" - TFT_eSprite-сумісна обгортка над Arduino_Canvas (offscreen
// framebuffer Arduino_GFX). Ідентична до TFT_eSprite з Setup_JD9853_C6.h
// (той самий Arduino_Canvas API, board-незалежний) - продубльована тут,
// бо кожен Setup_*.h файл в проєкті самодостатній (не шарить класи між
// платами).
class TFT_eSprite {
 public:
  explicit TFT_eSprite(TFT_eSPI *tft) : _tft(tft) {}
  ~TFT_eSprite() { delete _canvas; }

  void setColorDepth(uint8_t) {}  // канва Arduino_GFX завжди 16-біт RGB565 - no-op
  void setSwapBytes(bool) {}

  void *createSprite(int32_t w, int32_t h) {
    _canvas = new Arduino_Canvas(w, h, _tft);
    // GFX_SKIP_OUTPUT_BEGIN - ІНАКШЕ canvas->begin() повторно викликає
    // _tft->begin(), переініціалізовуючи SPI-шину дисплея (вже
    // проініціалізовану раніше в Display::init() -> tft_.init()). Без
    // цього прапорця - подвійна ініціалізація SPI на льоту (та сама
    // проблема, що й в Setup_JD9853_C6.h).
    if (!_canvas->begin(GFX_SKIP_OUTPUT_BEGIN)) {
      Logger::error("[TFT_eSprite/Arduino_Canvas] begin() failed - недостатньо памʼяті?");
      delete _canvas;
      _canvas = nullptr;
      return nullptr;
    }
    // Канва щойно з'явилась - застосовуємо шрифт, обраний до її створення.
    _fontHeight = ArduinoGfxFonts::apply(_canvas, _textFont);
    return static_cast<void *>(_canvas);
  }

  void fillSprite(uint16_t color) {
    if (_canvas) _canvas->fillScreen(color);
  }

  void pushSprite(int32_t x, int32_t y) {
    if (!_canvas) return;
    _tft->draw16bitRGBBitmap(x, y, _canvas->getFramebuffer(), _canvas->width(), _canvas->height());
  }

  void setCursor(int32_t x, int32_t y) {
    if (_canvas) _canvas->setCursor(x, y);
  }
  void setTextColor(uint16_t c) {
    if (_canvas) _canvas->setTextColor(c);
  }
  void setTextColor(uint16_t c, uint16_t bg) {
    if (_canvas) _canvas->setTextColor(c, bg);
  }
  void setTextSize(uint8_t s) {
    _textSize = s;
    if (_canvas) _canvas->setTextSize(s);
  }
  // Вибір шрифту за номером TFT_eSPI. Номер запам'ятовується навіть якщо
  // канви ще немає (createSprite() викликається пізніше) - інакше шрифт,
  // виставлений до створення спрайта, мовчки губився б.
  void setTextFont(uint8_t font) {
    _textFont = font;
    if (_canvas) _fontHeight = ArduinoGfxFonts::apply(_canvas, font);
  }
  void setTextDatum(uint8_t datum) { _datum = datum; }

  size_t fontHeight() { return _fontHeight * _textSize; }

  int16_t textWidth(const char *t) {
    if (!_canvas) return 0;
    int16_t x1, y1;
    uint16_t w, h;
    _canvas->getTextBounds(t, 0, 0, &x1, &y1, &w, &h);
    return static_cast<int16_t>(w);
  }

  uint16_t drawString(const char *t, int32_t x, int32_t y) {
    if (!_canvas) return 0;
    int16_t x1, y1;
    uint16_t w, h;
    _canvas->getTextBounds(t, 0, 0, &x1, &y1, &w, &h);
    if (_datum == MC_DATUM) {
      x -= static_cast<int32_t>(w) / 2;
      y -= static_cast<int32_t>(h) / 2;
    }
    _canvas->setCursor(x, y);
    _canvas->print(t);
    return static_cast<uint16_t>(w);
  }

  void drawRect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color) {
    if (_canvas) _canvas->drawRect(x, y, w, h, color);
  }
  void drawCircle(int32_t x, int32_t y, int32_t r, uint32_t color) {
    if (_canvas) _canvas->drawCircle(x, y, r, color);
  }
  void drawBitmap(int16_t x, int16_t y, const uint8_t *bitmap, int16_t w, int16_t h, uint16_t fg) {
    if (_canvas) _canvas->drawBitmap(x, y, const_cast<uint8_t *>(bitmap), w, h, fg);
  }
  void pushImage(int32_t x, int32_t y, int32_t w, int32_t h, const uint16_t *data) {
    if (_canvas) _canvas->draw16bitRGBBitmap(x, y, const_cast<uint16_t *>(data), w, h);
  }

  size_t print(const char *s) { return _canvas ? _canvas->print(s) : 0; }
  size_t println(const char *s) { return _canvas ? _canvas->println(s) : 0; }

  template <typename... Args>
  size_t printf(const __FlashStringHelper *ifsh, const Args &...args) {
    return _canvas ? _canvas->printf(reinterpret_cast<const char *>(ifsh), args...) : 0;
  }

  template <typename... Args>
  size_t printf(const char *format, const Args &...args) {
    return _canvas ? _canvas->printf(format, args...) : 0;
  }

 private:
  TFT_eSPI *_tft;
  Arduino_Canvas *_canvas = nullptr;
  uint8_t _textSize = 1;
  uint8_t _textFont = 1;    // номер шрифту TFT_eSPI, обраний setTextFont()
  uint8_t _fontHeight = 8;  // висота рядка цього шрифту при _textSize == 1
  uint8_t _datum = TL_DATUM;
};
