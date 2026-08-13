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
// ВАЖЛИВО: піни офсети (34, 0, 34, 0) — стандартні для JD9853-в-ST7789-режимі
// на цій платі за даними офіційної документації/прикладу Waveshare, але
// ротація/орієнтація (TFT_ROTATION) і остаточна коректність кольорів/офсетів
// потребують валідації на реальному пристрої.
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
#define TFT_TRANSPARENT 0x0120

// Датуми тексту (підмножина TFT_eSPI, якої вистачає src/Display.cpp)
#define TL_DATUM 0
#define MC_DATUM 4

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
                        true /* IPS */, TFT_WIDTH, TFT_HEIGHT, 34 /* col offset1 */,
                        0 /* row offset1 */, 34 /* col offset2 */, 0 /* row offset2 */) {}

  void init() {
    if (!begin()) {
      Logger::error("[TFT_eSPI/JD9853] begin() failed - перевір піни/проводку SPI");
    }
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
