// Display.h
#pragma once

#include <stdarg.h>  // Обов'язково для роботи з трикрапкою (...)

#include <TLogger.hpp>
#include <atomic>

#include "TftInstance.h"
// Для env:esp32-st7789     -> це справжній bodmer/TFT_eSPI (SPI, ST7789)
// Для env:esp32-4848s040   -> TFT_eSPI тут є alias'ом на LGFX (LovyanGFX,
//                             RGB-панель ST7701), визначеним у Setup_ST7701_4848S040.h
// Вибір відбувається через -DBOARD_4848S040 у build_flags конкретного env
// (TftInstance.h), прикладний код нижче однаковий для обох плат.
class Display {
public:
  void startWrite() {
    #if defined(DISPLAY_SPLIT_COUNT) && DISPLAY_SPLIT_COUNT > 0
    _activeSplitBlock = (_activeSplitBlock + 1) % DISPLAY_SPLIT_COUNT;
    // _logger.debug("active split block = %d", _activeSplitBlock); delay(600);
    #endif
    tft_.startWrite(); 
  }

  void endWrite() { 
    tft_.endWrite(); 
  }

  explicit Display();
  void flip();
  uint8_t getRotation() const { return tft_.getRotation(); }
  void setRotation(uint8_t r) { tft_.setRotation(r); }

  // Ініціалізація дисплея (обов'язково викликати в setup())
  void init();

  // Заливка всього екрану кольором
  void clear(uint16_t color = TFT_BLACK);

  // Малювання тексту в позиції (x, y)
  void drawText(int x, int y, const char *text, uint16_t color);

  // Малювання тексту по центру екрана
  // void drawCenteredText(const char *text, uint16_t color, uint8_t fontSize = 4);

  // Виводить накопичений у спрайті кадр на реальний екран.
  // Викликати після того, як усе малювання кадру завершено.
  void flush();

  uint8_t brightness() { return brightness_; }  // percent!
  void brightness(uint8_t percent);

  void pushImage(int32_t x, int32_t y, int32_t w, int32_t h, const uint16_t *data);
  // RGB332 (8bpp) буфер - НЕ приводити до pushImage(uint16_t*): TFT_eSPI прочитає
  // його з подвоєним stride (2 байти/піксель замість 1), що дає ефект "картинка
  // порізана на 4 квадрати" (половинний буфер читається з подвоєним кроком рядка).
  // esp32-c6: канва Arduino_GFX не має 8bpp - тут дані конвертуються в RGB565
  // рядок за рядком (див. Display.cpp).
  void pushImage8bpp(int32_t x, int32_t y, int32_t w, int32_t h, const uint8_t *data);
  void setCursor(int32_t x, int32_t y);

  // Ширина/висота активної області екрану (з урахуванням rotation)
  int width() const;
  int height() const;

  // Без const на типі повернення: для скаляра він не має сенсу й ігнорується
  // компілятором (-Wignored-qualifiers).
  uint32_t loopFrameRate();
  size_t fontHeight() { return sprite().fontHeight(); }

  void setTextFont(uint8_t f) { sprite().setTextFont(f); }
  void setTextColor(uint16_t color) { sprite().setTextColor(color); }
  void setTextColor(uint16_t color, uint16_t bg) { sprite().setTextColor(color, bg); }
  void setTextSize(uint8_t size) { sprite().setTextSize(size); }

  void drawRect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color) {
    dXY(&x, &y); sprite().drawRect(x, y, w, h, color);
  }

  void drawCircle(int32_t x, int32_t y, int32_t r, uint32_t color) {
    dXY(&x, &y); 
    sprite().drawCircle(x, y, r, color);
  }

  /* uint16_t drawString(const char *text, int32_t x, int32_t y) {
    dXY(&x, &y); 
    return sprite().drawString(text, x, y);
  } */

  int16_t textWidth(const char *string) { return sprite().textWidth(string); }

  size_t print(const char *string) { return sprite().print(string); }
  size_t println(const char *string) { return sprite().println(string); }
  /* void getTextBounds(const char *str, int16_t x, int16_t y,
                  int16_t *x1, int16_t *y1, uint16_t *w, uint16_t *h)
  {
      sprite_.getTextBounds(str, x, y, x1, y1, w, h);
  } */

  void drawBitmap( int16_t x, int16_t y, const uint8_t *bitmap, int16_t w, int16_t h, uint16_t fgcolor) {
    dXY(&x, &y); 
    sprite().drawBitmap(x, y, bitmap, w, h, fgcolor);
  };
    /*drawBitmap( int16_t x, int16_t y, const uint8_t *bitmap, int16_t w, int16_t h, uint16_t fgcolor, uint16_t bgcolor),
    drawXBitmap(int16_t x, int16_t y, const uint8_t *bitmap, int16_t w, int16_t h, uint16_t fgcolor),
    drawXBitmap(int16_t x, int16_t y, const uint8_t *bitmap, int16_t w, int16_t h, uint16_t fgcolor, uint16_t bgcolor),*/

  template <typename T>
  void dXY(T* x, T* y) {
    #if defined(DISPLAY_SPLIT_COUNT) && DISPLAY_SPLIT_COUNT > 0
    *y = *y - static_cast<T>(_activeSplitBlock * (height() / DISPLAY_SPLIT_COUNT));
    #endif
  }

  template <typename... Args>
  size_t printf(const __FlashStringHelper *ifsh, const Args &...args) {
    // return sprite_.printf(reinterpret_cast<const char*>(ifsh), args...);
    return sprite().printf((PGM_P)ifsh, args...);
  }

  template <typename... Args>
  size_t printf(const char *format, const Args &...args) {
    return sprite().printf(format, args...);
  }

  // void getTextBound() { sprite().getTextBound(); };
protected:
#if defined(DISPLAY_SPLIT_COUNT) && DISPLAY_SPLIT_COUNT
  void initSprite();
#endif

  // Повертає посилання точного типу (TFT_eSprite& при увімкненому буфері,
  // TFT_eSPI& — при вимкненому), вибір фіксується на етапі КОМПІЛЯЦІЇ
  // через build flag DISPLAY_SPLIT_COUNT.
  //
  // Це принципово важливо: pushImage() (і інші методи TFT_eSPI/TFT_eSprite)
  // НЕ virtual, тому виклик через посилання звужене до базового TFT_eSPI&
  // завжди резолвиться в TFT_eSPI::pushImage() навіть якщо реальний об'єкт —
  // TFT_eSprite (name hiding, не поліморфізм). Єдиний спосіб уникнути цього —
  // щоб тип ПОВЕРНЕННЯ sprite() збігався з реальним типом об'єкта.
  //
  // Тут #if (не if constexpr) свідомо: потрібна різна СИГНАТУРА методу
  // (різний тип повернення — TFT_eSprite& чи TFT_eSPI&), а if constexpr не
  // може змінювати тип повернення функції — лише розгалужувати тіло з уже
  // фіксованим типом. Дві версії методу, обрані препроцесором, — єдиний
  // спосіб отримати саме той конкретний тип, який потрібен для коректної
  // (не-віртуальної) диспетчеризації.
#if defined(DISPLAY_SPLIT_COUNT) && DISPLAY_SPLIT_COUNT
  TFT_eSprite& sprite() { return sprite_; }
#else
  TFT_eSPI& sprite() { return tft_; }
#endif

private:
  TFT_eSPI &tft_;
#if defined(DISPLAY_SPLIT_COUNT) && DISPLAY_SPLIT_COUNT
  int8_t _activeSplitBlock = 0;
  TFT_eSprite sprite_;  // вся робота з екраном (drawText/clear/...) йде через спрайт,
                        // на реальний дисплей кадр потрапляє лише через flush()
#endif

  int width_ = 0;
  int height_ = 0;
  uint8_t brightness_ = 50;  // percent!

  // Переюзний рядковий буфер для pushImage8bpp() (RGB332 -> RGB565).
  // Раніше він malloc/free-ився на КОЖНОМУ виклику, тобто щокадру при
  // малюванні фону. Виділяється лениво і росте лише за потреби; звільняється
  // разом з Display (об'єкт глобальний, тобто фактично ніколи).
  uint16_t* rowBuffer_ = nullptr;
  int32_t rowBufferPx_ = 0;

  const TLogger _logger{"tft"};
};
