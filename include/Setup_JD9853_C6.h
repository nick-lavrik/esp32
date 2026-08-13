// Setup_ArduinoGFX_C6.h
// TFT_eSPI-сумісний шар над moononournation/Arduino_GFX для JD9853 (172x320).
//
// Плата: Waveshare ESP32-C6-LCD-1.47 (ESP32-C6, JD9853 SPI, 172x320, без PSRAM)
// https://www.aliexpress.com/item/1005008207509770.html
// https://docs.waveshare.com/ESP32-C6-Touch-LCD-1.47
//
// ПРИЧИНА: bodmer/TFT_eSPI і lovyan03/LovyanGFX (вже використовуються в
// проєкті) не мають драйвера JD9853. Контролер командно-сумісний з ST7789
// (підтверджено офіційною демкою Waveshare і moononournation/Arduino_GFX
// issue #693) — тому тут третя графічна бібліотека лише для цієї плати,
// обгорнута у TFT_eSPI/TFT_eSprite-сумісний фасад (той самий підхід, що й
// Setup_SSD1306_NodeMCU.h для esp8266), щоб src/Display.h/.cpp (спільний
// прикладний код для ВСІХ плат) лишались без змін.
//
// Підключається через src/TftInstance.h за BOARD_ESP32_C6 (аналогічно тому,
// як Setup_ST7701_4848S040.h підключається за BOARD_4848S040).
//
// ВАЖЛИВО: піни, офсети (34, 0, 34, 0) і IPS=false підтверджені офіційною
// демкою Waveshare для цієї ж плати (wiki ESP32-C6-Touch-LCD-1.47) і робочим
// прикладом на GitHub (moononournation/Arduino_GFX, discussion #693) —
// саме неправильний IPS (був true, треба false) давав білий/інвертований
// екран. Ротація (TFT_ROTATION) і мірорринг залежать від фізичного монтажу
// панелі в корпусі — якщо після фіксу IPS зображення дзеркальне, спробуйте
// TFT_ROTATION 1/2/3 (build_flags у platformio.ini) замість 0.
//
// TF-картка на цій платі — звичайний SPI, ШИНА СПІЛЬНА З ДИСПЛЕЄМ
// (SCK=1, MOSI=2, окремі MISO=3 і CS=4) — на відміну від esp32-s3-lcd147
// (там SD_MMC). SDCardInspector/SD.begin(SD_CS) підключаються так само,
// як на esp32-st7789/esp32-4848s040.

#pragma once

#include <Arduino.h>
#include <Arduino_GFX_Library.h>

#include <Logger.hpp>

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

// ---------- Кольори (RGB565) — ті самі константи, яких потребує src/Display.h/.cpp ----------
#define TFT_BLACK 0x0000  // було 0x0000
#define TFT_WHITE 0xFFFF  // було 0xFFFF
#define TFT_RED 0x001F  // було 0xF800
#define TFT_GREEN 0x07E0  // було 0x07E0
#define TFT_DARKGREEN 0x03E0  // було 0x03E0
#define TFT_YELLOW 0x07FF  // було 0xFFE0
#define TFT_CYAN 0xFFE0  // було 0x07FF
#define TFT_MAGENTA 0xF81F  // було 0xF81F
#define TFT_ORANGE 0x05BF  // було 0xFDA0
#define TFT_DARKGREY 0x7BEF  // було 0x7BEF
#define TFT_LIGHTGREY 0xC618  // було 0xC618

// Датуми тексту (підмножина TFT_eSPI, якої вистачає src/Display.cpp)
#define TL_DATUM 0
#define MC_DATUM 4


static const uint8_t __st7789_type1_init_operations[] = {
    BEGIN_WRITE,
    WRITE_COMMAND_8, ST7789_SLPOUT, // 2: Out of sleep mode, no args, w/delay
    END_WRITE,

    DELAY, ST7789_SLPOUT_DELAY,

    BEGIN_WRITE,
    WRITE_C8_D8, ST7789_COLMOD, 0x55, // 3: Set color mode, 16-bit color
    // WRITE_C8_D8, 0x36, 0x00, // Стандарт
    // WRITE_C8_D8, 0x36, 0x40, // Дзеркало по горизонталі (MX=1)
    // WRITE_C8_D8, 0x36, 0x80, // Дзеркало по вертикалі (MY=1)
    WRITE_C8_D8, 0x36, 0x60, // Обмін X та Y + Дзеркало

    WRITE_C8_BYTES, 0xB0, 2,
    0x00, 0xF0, // 0xF0 MSB first, 0xF8 LSB first

    WRITE_C8_BYTES, 0xB2, 5,
    0x0C, 0x0C, 0x00, 0x33, 0x33,

    WRITE_C8_D8, 0xB7, 0x35,
    WRITE_C8_D8, 0xBB, 0x19,
    WRITE_C8_D8, 0xC0, 0x2C,
    WRITE_C8_D8, 0xC2, 0x01,
    WRITE_C8_D8, 0xC3, 0x12,
    WRITE_C8_D8, 0xC4, 0x20,
    WRITE_C8_D8, 0xC6, 0x0F,

    WRITE_C8_D16, 0xD0, 0xA4, 0xA1,

    WRITE_C8_BYTES, 0xE0, 14,
    0b11110000, // V63P3, V63P2, V63P1, V63P0,  V0P3,  V0P2,  V0P1,  V0P0
    0b00001001, //     0,     0,  V1P5,  V1P4,  V1P3,  V1P2,  V1P1,  V1P0
    0b00010011, //     0,     0,  V2P5,  V2P4,  V2P3,  V2P2,  V2P1,  V2P0
    0b00010010, //     0,     0,     0,  V4P4,  V4P3,  V4P2,  V4P1,  V4P0
    0b00010010, //     0,     0,     0,  V6P4,  V6P3,  V6P2,  V6P1,  V6P0
    0b00101011, //     0,     0,  J0P1,  J0P0, V13P3, V13P2, V13P1, V13P0
    0b00111100, //     0, V20P6, V20P5, V20P4, V20P3, V20P2, V20P1, V20P0
    0b01000100, //     0, V36P2, V36P1, V36P0,     0, V27P2, V27P1, V27P0
    0b01001011, //     0, V43P6, V43P5, V43P4, V43P3, V43P2, V43P1, V43P0
    0b00011011, //     0,     0,  J1P1,  J1P0, V50P3, V50P2, V50P1, V50P0
    0b00011000, //     0,     0,     0, V57P4, V57P3, V57P2, V57P1, V57P0
    0b00010111, //     0,     0,     0, V59P4, V59P3, V59P2, V59P1, V59P0
    0b00011101, //     0,     0, V61P5, V61P4, V61P3, V61P2, V61P1, V61P0
    0b00100001, //     0,     0, V62P5, V62P4, V62P3, V62P2, V62P1, V62P0

    WRITE_C8_BYTES, 0XE1, 14,
    0b11110000, // V63P3, V63P2, V63P1, V63P0,  V0P3,  V0P2,  V0P1,  V0P0
    0b00001001, //     0,     0,  V1P5,  V1P4,  V1P3,  V1P2,  V1P1,  V1P0
    0b00010011, //     0,     0,  V2P5,  V2P4,  V2P3,  V2P2,  V2P1,  V2P0
    0b00001100, //     0,     0,     0,  V4N4,  V4N3,  V4N2,  V4N1,  V4N0
    0b00001101, //     0,     0,     0,  V6N4,  V6N3,  V6N2,  V6N1,  V6N0
    0b00100111, //     0,     0,  J0N1,  J0N0, V13N3, V13N2, V13N1, V13N0
    0b00111011, //     0, V20N6, V20N5, V20N4, V20N3, V20N2, V20N1, V20N0
    0b01000100, //     0, V36N2, V36N1, V36N0,     0, V27N2, V27N1, V27N0
    0b01001101, //     0, V43N6, V43N5, V43N4, V43N3, V43N2, V43N1, V43N0
    0b00001011, //     0,     0,  J1N1,  J1N0, V50N3, V50N2, V50N1, V50N0
    0b00010111, //     0,     0,     0, V57N4, V57N3, V57N2, V57N1, V57N0
    0b00010111, //     0,     0,     0, V59N4, V59N3, V59N2, V59N1, V59N0
    0b00011101, //     0,     0, V61N5, V61N4, V61N3, V61N2, V61N1, V61N0
    0b00100001, //     0,     0, V62N5, V62N4, V62N3, V62N2, V62N1, V62N0

    WRITE_COMMAND_8, ST7789_NORON, // 4: Normal display on, no args, w/delay
    END_WRITE,

    DELAY, 10,

    BEGIN_WRITE,
    WRITE_COMMAND_8, ST7789_DISPON, // 5: Main screen turn on, no args, w/delay
    END_WRITE};

static const uint8_t __st7789_type2_init_operations[] = {
    BEGIN_WRITE,
    WRITE_C8_D8, ST7789_COLMOD, 0x55, // 3: Set color mode, 16-bit color

    WRITE_C8_BYTES, 0xB2, 5,
    0x0C, 0x0C, 0x00, 0x33, 0x33,

    WRITE_C8_D8, 0xB4, 0x01,
    WRITE_C8_D16, 0xC0, 0x2C, 0x2D,
    WRITE_C8_D8, 0xC5, 0x2E,

    WRITE_COMMAND_8, ST7789_SLPOUT,
    END_WRITE,

    DELAY, ST7789_SLPOUT_DELAY,

    BEGIN_WRITE,
    WRITE_COMMAND_8, ST7789_DISPON,
    END_WRITE};

static const uint8_t __st7789_type3_init_operations[] = {
    BEGIN_WRITE,
    WRITE_C8_D8, ST7789_COLMOD, 0x55, // 3: Set color mode, 16-bit color
    WRITE_COMMAND_8, ST7789_SLPOUT,
    END_WRITE,

    DELAY, ST7789_SLPOUT_DELAY,

    BEGIN_WRITE,
    WRITE_COMMAND_8, ST7789_DISPON,
    END_WRITE};



// "Пристрій" - фізичний JD9853/ST7789-сумісний дисплей. Публічний API, яким
// користується src/Display.h/.cpp (init/setRotation/getRotation/width/height/
// startWrite/endWrite/fillScreen/setCursor/print/...), успадкований від
// Arduino_ST7789/Arduino_GFX без змін; тут додається лише те, чого немає
// (init() з підсвіткою, textWidth/drawString-обгортки в стилі TFT_eSPI).
class TFT_eSPI : public Arduino_ST7789 {
 public:
  TFT_eSPI()
      : Arduino_ST7789(new Arduino_ESP32SPI(TFT_DC, TFT_CS, TFT_SCLK, TFT_MOSI, TFT_MISO),
                        TFT_RST, 0 /* rotation - виставляється окремо через setRotation() */,
                        false /* true IPS */, TFT_WIDTH, TFT_HEIGHT, 34 /* col offset1 */,
                        0 /* row offset1 */, 34 /* col offset2 */, 0 /* row offset2 */,
                        __st7789_type1_init_operations, sizeof(__st7789_type1_init_operations)) {}

  void init() {
    if (!begin()) {
      Logger::error("[TFT_eSPI/JD9853] begin() failed - перевір піни/проводку SPI");
    }

    // Передача команди безпосередньо в контролер ST7789
    // tft.writeCommand(0x36); // Регістр MADCTL (Memory Data Access Control)

    // Залежно від вашої бібліотеки та поточної орієнтації, 
    // спробуйте підібрати одне з цих значень:
    // tft.writeData(0x00); // Стандартний режим
    // tft.writeData(0x40); // Дзеркало по Y
    // tft.writeData(0x20); // Дзеркало по X
    // tft.writeData(0x60); // Обмін X та Y + Дзеркало

#if defined(TFT_BL)
    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, TFT_BACKLIGHT_ON);
#endif
  }

  // Arduino_GFX сам керує порядком байтів при передачі RGB565 по SPI -
  // no-op, лишений лише для сумісності сигнатури з src/Display.h.
  void setSwapBytes(bool) {}

  // Arduino_GFX не має власного поняття "висота рядка шрифту" в API
  // src/Display.h - вбудований (не-u8g2) шрифт має комірку 8px по висоті,
  // як і в Adafruit_GFX/TFT_eSPI за замовчуванням.
  size_t fontHeight() { return 8; }

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
    if (!_canvas->begin()) {
      Logger::error("[TFT_eSprite/Arduino_Canvas] begin() failed - недостатньо памʼяті?");
      delete _canvas;
      _canvas = nullptr;
      return nullptr;
    }
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
  void setTextFont(uint8_t) {}  // альтернативних (u8g2) шрифтів тут не підключено - no-op
  void setTextDatum(uint8_t datum) { _datum = datum; }

  size_t fontHeight() { return 8 * _textSize; }  // вбудований шрифт: комірка 8px по висоті

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
  uint8_t _datum = TL_DATUM;
};
