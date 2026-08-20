// Setup_JD9853_C6.h
// TFT_eSPI-сумісний шар над moononournation/Arduino_GFX для JD9853 (172x320).
//
// Плата: Waveshare ESP32-C6-LCD-1.47 (ESP32-C6, JD9853 SPI, 172x320, без PSRAM)
// https://www.aliexpress.com/item/1005008207509770.html
// https://docs.waveshare.com/ESP32-C6-Touch-LCD-1.47
//
// ПРИЧИНА: bodmer/TFT_eSPI і lovyan03/LovyanGFX (вже використовуються в
// проєкті) не мають драйвера JD9853. Контролер лише БАЗОВО командно-
// сумісний з ST7789 (той самий opcode-набір WRITE_COMMAND_8/WRITE_C8_D8/...
// з Arduino_GFX) - але generic ST7789 init-послідовність з бібліотеки
// (яку викликає звичайний begin()) дає НЕПРАВИЛЬНИЙ результат: білий
// екран (з IPS=true), дзеркало, неправильні кольори (з IPS=false).
// Справжня причина - JD9853 потребує ВЛАСНУ init-послідовність з
// вендорським unlock-регістром (0xDF 0x98 0x53) і повним набором
// gamma/voltage-регістрів, яких у generic ST7789-init просто немає.
//
// Ця послідовність (jd9853_reg_init_operations нижче) взята з
// andreimagic/ESP32_C6_Touch_LCD_1_47_LVGL_Animated_Clock (MIT), той
// самий Waveshare ESP32-C6-Touch-LCD-1.47, перевірено автором на
// реальному пристрої - дисплей/SD/IMU/консоль/WiFi стабільні, без жодних
// програмних компенсацій (свап R/B, нестандартна ротація тощо), які були
// в попередніх ітераціях цього файлу і які тепер прибрано.
//
// Третя графічна бібліотека лише для цієї плати, обгорнута у
// TFT_eSPI/TFT_eSprite-сумісний фасад (той самий підхід, що й
// Setup_SSD1306_NodeMCU.h для esp8266), щоб src/Display.h/.cpp (спільний
// прикладний код для ВСІХ плат) лишались без змін.
//
// Підключається через src/TftInstance.h за BOARD_ESP32_C6 (аналогічно тому,
// як Setup_ST7701_4848S040.h підключається за BOARD_4848S040).
//
// SPI-шина - ЄДИНА, СПІЛЬНА з TF-карткою (Arduino_HWSPI їде на глобальному
// об'єкті `SPI`, не створює власний хост). `SPI.begin(SD_SCK, SD_MISO,
// SD_MOSI, SD_CS)` викликається в src/main.cpp::setupSD() - обов'язково
// ПЕРЕД setupDisplay() (вже так у поточному порядку setup()). Раніше тут
// був окремий Arduino_ESP32SPI з власним SPI-хостом на тих самих пінах,
// що конфліктувало з SD (perimanSetPinBus: No deinit function for type
// SPI_MASTER_SCK/MOSI) і, ймовірно, спричиняло частину нестабільності.
// Піни: SCK=1, MOSI=2, окремі MISO=3 (лише SD) і CS=4 (SD) / 14 (LCD).

#pragma once

#include <Arduino.h>
#include <Arduino_GFX_Library.h>

#include <Logger.hpp>

#include "ArduinoGfxFonts.h"

// ---------- Піни SPI дисплея (JD9853) ----------
// Джерело: docs.waveshare.com/ESP32-C6-Touch-LCD-1.47
#define TFT_WIDTH 172
#define TFT_HEIGHT 320
#define TFT_SCLK 1
#define TFT_MOSI 2
#define TFT_MISO -1  // дисплей не має read-back лінії (write-only)
#define TFT_CS 14
#define TFT_DC 15
#define TFT_RST 22
#define TFT_BL 23
#define TFT_BACKLIGHT_ON HIGH

// ---------- Кольори (RGB565) — стандартні значення TFT_eSPI/Adafruit_GFX.
// Попередній свап R/B був компенсацією за НЕПРАВИЛЬНУ (generic ST7789)
// init-послідовність. З правильною, апаратно перевіреною послідовністю
// для JD9853 (jd9853RegInit() нижче) свап не потрібен - підтверджено
// референсним проєктом на реальному пристрої (andreimagic/
// ESP32_C6_Touch_LCD_1_47_LVGL_Animated_Clock, MIT).
#define TFT_BLACK 0x0000
#define TFT_WHITE 0xFFFF
#define TFT_RED 0xF800
#define TFT_GREEN 0x07E0
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

// Справжня, апаратно підтверджена init-послідовність JD9853 (НЕ generic
// ST7789 з бібліотеки Arduino_GFX - той дає білий екран/неправильні
// кольори/дзеркало, бо це різні чіпи, лише командно-сумісні на базовому
// рівні). Джерело: andreimagic/ESP32_C6_Touch_LCD_1_47_LVGL_Animated_Clock
// (lcd_reg_init(), MIT), той самий Waveshare ESP32-C6-Touch-LCD-1.47,
// перевірено автором на реальному пристрої (дисплей/SD/IMU/консоль/WiFi
// стабільні). Викликається з init() ПІСЛЯ begin() (яке лишень відкриває
// SPI-шину і шле generic ST7789 sleep-out/color-mode, не критично що саме)
// і ПЕРЕД setRotation() (0x36 тут навмисно 0x00 - фінальний MADCTL все
// одно перезаписує бібліотечний Arduino_ST7789::setRotation()).
static const uint8_t jd9853_reg_init_operations[] = {
    BEGIN_WRITE,
    WRITE_COMMAND_8, 0x11,
    END_WRITE,
    DELAY, 120,
    BEGIN_WRITE,
    WRITE_C8_D16, 0xDF, 0x98, 0x53,  // unlock vendor-specific регістри
    WRITE_C8_D8, 0xB2, 0x23,
    WRITE_COMMAND_8, 0xB7,
    WRITE_BYTES, 4, 0x00, 0x47, 0x00, 0x6F,
    WRITE_COMMAND_8, 0xBB,
    WRITE_BYTES, 6, 0x1C, 0x1A, 0x55, 0x73, 0x63, 0xF0,
    WRITE_C8_D16, 0xC0, 0x44, 0xA4,
    WRITE_C8_D8, 0xC1, 0x16,
    WRITE_COMMAND_8, 0xC3,
    WRITE_BYTES, 8, 0x7D, 0x07, 0x14, 0x06, 0xCF, 0x71, 0x72, 0x77,
    WRITE_COMMAND_8, 0xC4,
    WRITE_BYTES, 12, 0x00, 0x00, 0xA0, 0x79, 0x0B, 0x0A, 0x16, 0x79, 0x0B, 0x0A, 0x16, 0x82,
    WRITE_COMMAND_8, 0xC8,
    WRITE_BYTES, 32,
    0x3F, 0x32, 0x29, 0x29, 0x27, 0x2B, 0x27, 0x28,
    0x28, 0x26, 0x25, 0x17, 0x12, 0x0D, 0x04, 0x00,
    0x3F, 0x32, 0x29, 0x29, 0x27, 0x2B, 0x27, 0x28,
    0x28, 0x26, 0x25, 0x17, 0x12, 0x0D, 0x04, 0x00,
    WRITE_COMMAND_8, 0xD0,
    WRITE_BYTES, 5, 0x04, 0x06, 0x6B, 0x0F, 0x00,
    WRITE_C8_D16, 0xD7, 0x00, 0x30,
    WRITE_C8_D8, 0xE6, 0x14,
    WRITE_C8_D8, 0xDE, 0x01,
    WRITE_COMMAND_8, 0xB7,
    WRITE_BYTES, 5, 0x03, 0x13, 0xEF, 0x35, 0x35,
    WRITE_COMMAND_8, 0xC1,
    WRITE_BYTES, 3, 0x14, 0x15, 0xC0,
    WRITE_C8_D16, 0xC2, 0x06, 0x3A,
    WRITE_C8_D16, 0xC4, 0x72, 0x12,
    WRITE_C8_D8, 0xBE, 0x00,
    WRITE_C8_D8, 0xDE, 0x02,
    WRITE_COMMAND_8, 0xE5,
    WRITE_BYTES, 3, 0x00, 0x02, 0x00,
    WRITE_COMMAND_8, 0xE5,
    WRITE_BYTES, 3, 0x01, 0x02, 0x00,
    WRITE_C8_D8, 0xDE, 0x00,
    WRITE_C8_D8, 0x35, 0x00,
    WRITE_C8_D8, 0x3A, 0x05,
    WRITE_COMMAND_8, 0x2A,
    WRITE_BYTES, 4, 0x00, 0x22, 0x00, 0xCD,
    WRITE_COMMAND_8, 0x2B,
    WRITE_BYTES, 4, 0x00, 0x00, 0x01, 0x3F,
    WRITE_C8_D8, 0xDE, 0x02,
    WRITE_COMMAND_8, 0xE5,
    WRITE_BYTES, 3, 0x00, 0x02, 0x00,
    WRITE_C8_D8, 0xDE, 0x00,
    WRITE_C8_D8, 0x36, 0x00,  // тимчасовий MADCTL - перезапишеться setRotation()
    WRITE_COMMAND_8, 0x21,
    END_WRITE,
    DELAY, 10,
    BEGIN_WRITE,
    WRITE_COMMAND_8, 0x29,
    END_WRITE};

// "Пристрій" - фізичний JD9853/ST7789-сумісний дисплей. Публічний API, яким
// користується src/Display.h/.cpp (init/setRotation/getRotation/width/height/
// startWrite/endWrite/fillScreen/setCursor/print/...), успадкований від
// Arduino_ST7789/Arduino_GFX без змін; тут додається лише те, чого немає
// (init() з підсвіткою, textWidth/drawString-обгортки в стилі TFT_eSPI).
// Arduino_ST7789(new Arduino_ESP32SPI(TFT_DC, TFT_CS, TFT_SCLK, TFT_MOSI),
class TFT_eSPI : public Arduino_ST7789 {
 public:
  TFT_eSPI()
      : Arduino_ST7789(_bus = new Arduino_HWSPI(TFT_DC, TFT_CS, TFT_SCLK, TFT_MOSI), TFT_RST,
                        0 /* rotation - виставляється окремо через setRotation() */,
                        false /* IPS */, TFT_WIDTH, TFT_HEIGHT, 34 /* col offset1 */,
                        0 /* row offset1 */, 34 /* col offset2 */, 0 /* row offset2 */) {}

  void init() {
    if (!begin()) {
      Logger::error("[TFT_eSPI/JD9853] begin() failed - перевір піни/проводку SPI");
    }
    // Справжня реєстрова послідовність JD9853 (див. коментар вище) -
    // ЗАМІНЮЄ generic ST7789 init, який begin() щойно надіслав.
    _bus->batchOperation(jd9853_reg_init_operations, sizeof(jd9853_reg_init_operations));

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
  // в Arduino_ST7789), бо базовий _bus у бібліотеці protected/недоступний
  // напряму для batchOperation() поза родиною Arduino_TFT.
  //
  // НАВМИСНО БЕЗ "= nullptr" тут! Присвоєння _bus = new Arduino_HWSPI(...)
  // в списку аргументів базового конструктора (нижче) виконується ПІД ЧАС
  // обчислення цих аргументів - до того, як власна фаза ініціалізації
  // членів похідного класу взагалі починається. Якби тут був default
  // member initializer (= nullptr), він спрацював би ПІСЛЯ виклику
  // базового конструктора Arduino_ST7789(...) і затер би щойно присвоєне
  // значення - саме це й стався: init() потім викликав _bus->batchOperation()
  // на nullptr (Guru Meditation: Load access fault, Setup_JD9853_C6.h:174).
  Arduino_DataBus *_bus;
};

/**
 * єдиний глобальний екземпляр, визначений в
 * @see file://./../src-esp32-c6/TftInstance.cpp
 */
extern TFT_eSPI tft;

// "Спрайт" - TFT_eSprite-сумісна обгортка над Arduino_Canvas (offscreen
// framebuffer Arduino_GFX). На відміну від TFT_eSprite (bodmer/TFT_eSPI),
// Arduino_Canvas не має pushSprite(x, y) з довільними рантайм-координатами
// (output_x/output_y фіксуються при створенні) - тому pushSprite() тут
// напряму пише framebuffer канви в tft_ через draw16bitRGBBitmap(), що дає
// той самий рантайм-контроль позиції, який потребує DISPLAY_SPLIT_COUNT
// (src/Display.cpp::flush() зсуває y для кожного активного split-блоку).
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
    // проініціалізовану раніше в Display::init() -> tft_.init()).
    // Без цього прапорця на реальному пристрої видно
    // "addApbChangeCallback(): duplicate" і
    // "perimanSetPinBus(): No deinit function for type SPI_MASTER_SCK/MOSI"
    // при кожному старті - подвійна ініціалізація SPI на льоту.
    // (moononournation/Arduino_GFX, discussion #346)
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
