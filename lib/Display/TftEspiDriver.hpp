#pragma once

// TftEspiDriver - драйвер Display над контрактом TFT_eSPI/TFT_eSprite.
//
// Підключати не напряму, а через DisplayDriver.hpp: саме він підключає
// бекенд (справжній bodmer/TFT_eSPI або фасад include/Setup_*.h), тобто
// визначає типи TFT_eSPI/TFT_eSprite, з якими тут працюємо.
//
// Контракт драйвера (те, що кличе Display; новий драйвер має дати те саме):
//   панель   - init(rotation), width/height, get/setRotation,
//              startWrite/endWrite, setBrightness(percent);
//   полотно  - beginCanvas(w, h, depth), pushCanvas(y), canvasPixels(),
//              kCanvasSwapped565 (лише при DISPLAY_SPLIT_COUNT > 0);
//   малювання - на поточну ціль (полотно смуги або сама панель):
//              fillScreen/fillRect/drawRect/drawCircle/drawBitmap/
//              pushImage/pushImage8bpp/drawString, текст.
// Координати - вже в системі цілі: зсув смуги (Display::dXY()) і відсікання
// робить Display, драйвер про смуги не знає.
//
// Усе, що залежить від конкретного бекенду (BOARD_*), живе тут і в
// TftEspiDriver.cpp, а не в Display.
//
// Власного TLogger драйвер не має свідомо: кожен логер тягне 128-байтний
// буфер рядка (ILogger::_buffer), а помилок у драйвера дві. Він повертає
// false, і логує той, хто кликав (Display, тег "tft").

#include <Arduino.h>

#include "features.h"  // DISPLAY_SPLIT_COUNT/HAS_SCREEN_MIRROR (дефолт 0, якщо env не задав)

class TftEspiDriver {
public:
#if DISPLAY_SPLIT_COUNT > 0
  TftEspiDriver() : canvas_(&panel_) {}
#else
  TftEspiDriver() = default;
#endif

  // --- панель ---

  void init(uint8_t rotation);

  int16_t width() { return panel_.width(); }
  int16_t height() { return panel_.height(); }

  uint8_t getRotation() { return panel_.getRotation(); }
  void setRotation(uint8_t r) { panel_.setRotation(r); }

  void startWrite() { panel_.startWrite(); }
  void endWrite() { panel_.endWrite(); }

  // percent - уже обмежений 0..100 (це робить Display::brightness()).
  void setBrightness(uint8_t percent);

  // --- полотно (буфер однієї смуги кадру) ---
#if DISPLAY_SPLIT_COUNT > 0
  // false - не вистачило пам'яті на буфер.
  bool beginCanvas(int16_t w, int16_t h, uint8_t colorDepth);
  void pushCanvas(int32_t y) { canvas_.pushSprite(0, y); }

#if HAS_SCREEN_MIRROR
  // Пікселі полотна - те, що дзеркало екрана віддає у веб-портал.
  // Ім'я методу в бекендів різне, вміст - однаковий: RGB565, рядок за рядком.
  const uint16_t* canvasPixels() {
#if defined(BOARD_4848S040)
    return static_cast<const uint16_t*>(canvas_.getBuffer());  // LovyanGFX
#else
    return static_cast<const uint16_t*>(canvas_.getPointer());  // TFT_eSPI і шими Arduino_GFX
#endif
  }

  // LovyanGFX тримає 16-бітний спрайт у swap565 (старший байт першим).
  // Справжній bodmer/TFT_eSPI - так само: TFT_eSprite::drawPixel/fillRect/...
  // для 16bpp завжди роблять color=(color>>8)|(color<<8) перед записом у _img
  // (Extensions/Sprite.cpp) - це властивість запису в буфер, а не setSwapBytes()
  // (той керує лише pushImage() і сюди не стосується). Свопу нема лише в
  // Arduino_GFX Canvas (esp32-c6, esp32-c6-lcd096): writePixelPreclipped() кладе
  // *fb = color без перетворень. Клієнту порядок повідомляється заголовком
  // відповіді, тому перевертати байти на платі не треба - вистачає сказати, які
  // вони; помилка тут мовчазна - і плата, і картинка валідні, лише кольори на
  // сторінці розсипаються шумом.
#if defined(BOARD_4848S040)
  static constexpr bool kCanvasSwapped565 = true;   // LovyanGFX
#elif defined(BOARD_ESP32_C6) || defined(BOARD_ESP32_C6_LCD096)
  static constexpr bool kCanvasSwapped565 = false;  // Arduino_GFX Canvas
#else
  static constexpr bool kCanvasSwapped565 = true;   // справжній bodmer TFT_eSPI (esp32-st7789, ttgo-t1, esp32-s3-lcd147)
#endif
#endif  // HAS_SCREEN_MIRROR
#endif  // DISPLAY_SPLIT_COUNT > 0

  // --- малювання на поточну ціль ---

  void fillScreen(uint16_t color) {
#if DISPLAY_SPLIT_COUNT > 0
    canvas_.fillSprite(color);
#else
    panel_.fillScreen(color);
#endif
  }

  void fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color) { target().fillRect(x, y, w, h, color); }
  void drawRect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color) { target().drawRect(x, y, w, h, color); }
  void drawCircle(int32_t x, int32_t y, int32_t r, uint32_t color) { target().drawCircle(x, y, r, color); }
  void drawBitmap(int16_t x, int16_t y, const uint8_t* bitmap, int16_t w, int16_t h, uint16_t color) {
    target().drawBitmap(x, y, bitmap, w, h, color);
  }

  void pushImage(int32_t x, int32_t y, int32_t w, int32_t h, const uint16_t* data) {
    target().pushImage(x, y, w, h, data);
  }
  // RGB332 (8bpp) буфер - НЕ приводити до pushImage(uint16_t*): TFT_eSPI прочитає
  // його з подвоєним stride (2 байти/піксель замість 1), що дає ефект "картинка
  // порізана на 4 квадрати" (половинний буфер читається з подвоєним кроком рядка).
  // false - не вистачило пам'яті на рядковий буфер або бекенд не вміє 8bpp.
  bool pushImage8bpp(int32_t x, int32_t y, int32_t w, int32_t h, const uint8_t* data);

  void drawString(const char* text, int32_t x, int32_t y) { target().drawString(text, x, y); }
  void setCursor(int32_t x, int32_t y) { target().setCursor(x, y); }
  void setTextFont(uint8_t f) { target().setTextFont(f); }
  void setTextColor(uint16_t color) { target().setTextColor(color); }
  void setTextColor(uint16_t color, uint16_t bg) { target().setTextColor(color, bg); }
  void setTextSize(uint8_t size) { target().setTextSize(size); }
  size_t fontHeight() { return target().fontHeight(); }
  int16_t textWidth(const char* text) { return target().textWidth(text); }
  size_t print(const char* text) { return target().print(text); }
  size_t println(const char* text) { return target().println(text); }

  template <typename... Args>
  size_t printf(const char* format, const Args&... args) {
    return target().printf(format, args...);
  }

private:
  // Повертає посилання точного типу (TFT_eSprite& при увімкненому буфері,
  // TFT_eSPI& — при вимкненому), вибір фіксується на етапі КОМПІЛЯЦІЇ
  // через build flag DISPLAY_SPLIT_COUNT.
  //
  // Це принципово важливо: pushImage() (і інші методи TFT_eSPI/TFT_eSprite)
  // НЕ virtual, тому виклик через посилання звужене до базового TFT_eSPI&
  // завжди резолвиться в TFT_eSPI::pushImage() навіть якщо реальний об'єкт —
  // TFT_eSprite (name hiding, не поліморфізм). Єдиний спосіб уникнути цього —
  // щоб тип ПОВЕРНЕННЯ target() збігався з реальним типом об'єкта.
  //
  // Тут #if (не if constexpr) свідомо: потрібна різна СИГНАТУРА методу
  // (різний тип повернення), а if constexpr не може змінювати тип повернення
  // функції — лише розгалужувати тіло з уже фіксованим типом.
#if DISPLAY_SPLIT_COUNT > 0
  TFT_eSprite& target() { return canvas_; }
#else
  TFT_eSPI& target() { return panel_; }
#endif

  // Порядок полів важливий: canvas_ отримує &panel_ у конструкторі.
  TFT_eSPI panel_;
#if DISPLAY_SPLIT_COUNT > 0
  TFT_eSprite canvas_;  // уся робота з екраном іде через полотно,
                        // на панель кадр потрапляє лише через pushCanvas()
#endif

  // Переюзний рядковий буфер для pushImage8bpp() (RGB332 -> RGB565).
  // Раніше він malloc/free-ився на КОЖНОМУ виклику, тобто щокадру при
  // малюванні фону. Виділяється лениво і росте лише за потреби; звільняється
  // разом з драйвером (об'єкт глобальний, тобто фактично ніколи).
  uint16_t* rowBuffer_ = nullptr;
  int32_t rowBufferPx_ = 0;
};
