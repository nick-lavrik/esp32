// Display.cpp
#include "Display.h"

#include <stdarg.h>  // Обов'язково для роботи з трикрапкою (...)
#include <stdlib.h>  // malloc/free для тимчасового рядкового буфера в pushImage8bpp (BOARD_ESP32_C6)

#if defined(BOARD_ESP32_C6) || defined(BOARD_ESP32_S3_LCD147) || defined(BOARD_TTGO_T1) || defined(BOARD_ST7789)
// Arduino_Canvas (Arduino_GFX) не має 8bpp-режиму - канва завжди 16-біт
// RGB565 (див. Setup_JD9853_C6.h::TFT_eSprite::setColorDepth() - no-op).
// Конвертуємо RGB332 (3-3-2) назад у RGB565 (5-6-5) біт-реплікацією.
static inline uint16_t rgb332to565(uint8_t c) {
  uint8_t r3 = (c >> 5) & 0x07;
  uint8_t g3 = (c >> 2) & 0x07;
  uint8_t b2 = c & 0x03;
  uint16_t r5 = (uint16_t)((r3 << 2) | (r3 >> 1));  // 0..7   -> 0..31
  uint16_t g6 = (uint16_t)(g3 * 9);                 // 0..7   -> 0..63 (7*9=63)
  uint16_t b5 = (uint16_t)(b2 * 10);                // 0..3   -> 0..30 (~5-біт, похибка ≤1)
  return (uint16_t)((r5 << 11) | (g6 << 5) | b5);
}
#endif

// Глобальний об'єкт "tft" створюється рівно в одному файлі на середовище:
//   env:esp32-st7789      -> src/TftInstance_ST7789.cpp
//   env:esp32-4848s040    -> src/TftInstance_4848S040.cpp
// Тут ми лише беремо на нього посилання.

#if defined(DISPLAY_SPLIT_COUNT) && DISPLAY_SPLIT_COUNT
Display::Display() : tft_(tft), sprite_(&tft_) {}
#else
Display::Display() : tft_(tft) {}
#endif

void Display::init() {
  tft_.init();
  tft_.setRotation(TFT_ROTATION);

  // Регістр 0x36 керує відображенням. 
  // Біти MX (6-й) та MY (7-й) відповідають за дзеркальність по X та Y.
  // tft_.writeCommand(0x36); 
  // tft_.writeData(0); // Стандарт
  // gfx->writeData(0x40); // Дзеркало по горизонталі (MX=1)
  // gfx->writeData(0x80); // Дзеркало по вертикалі (MY=1)
  // gfx->writeData(0xC0); // Обидва (MX=1, MY=1)

  width_ = tft_.width();
  height_ = tft_.height();

#if defined(DISPLAY_SPLIT_COUNT) && DISPLAY_SPLIT_COUNT
  initSprite();
#endif

  sprite().setSwapBytes(true);
  // tft_.setSwapBytes(true);
  flush();  // одразу показуємо чорний кадр, щоб не лишався сміттєвий вміст VRAM
}

#if defined(DISPLAY_SPLIT_COUNT) && DISPLAY_SPLIT_COUNT
void Display::initSprite() {
  // Спрайт на весь розмір екрана — вся подальша робота йде через нього.
  sprite_.setColorDepth(SPRITE_COLOR_DEPTH);  // 16
  // sprite_.setColorDepth(5);  // 16
  void* buf = sprite_.createSprite(width_, height_ / DISPLAY_SPLIT_COUNT);
  _logger.info("initSprite(%d, %d, depth=%d)", width_, height_ / DISPLAY_SPLIT_COUNT, SPRITE_COLOR_DEPTH);
  if (buf == nullptr) {
    _logger.error("ПОМИЛКА: createSprite() не зміг виділити пам'ять!");
    _logger.error("Потрібно: %d байт, вільно (heap): %u байт", width_ * height_ * 2,
                  ESP.getFreeHeap());
  }

  sprite_.fillSprite(TFT_BLACK);
}
#endif

void Display::flip() {
  _logger.info("TFT.setRotation(%d)", (tft_.getRotation() + 2) % 4);
  tft_.setRotation((tft_.getRotation() + 2) % 4);
}

void Display::clear(uint16_t color) { 
#if defined(DISPLAY_SPLIT_COUNT) && DISPLAY_SPLIT_COUNT
  sprite_.fillSprite(color);
#else
  tft_.fillScreen(color);
#endif
}

void Display::drawText(int x, int y, const char* text, uint16_t color) {
  dXY(&x, &y); 
  sprite().setTextColor(color);
  sprite().drawString(text, x, y);
}

/* void Display::drawCenteredText(const char* text, uint16_t color, uint8_t fontSize) {
  sprite().setTextColor(color, TFT_TRANSPARENT);
  sprite().setTextSize(fontSize);
  sprite().setTextDatum(MC_DATUM);  // Middle-Center — спільний для TFT_eSPI і LovyanGFX
  sprite().drawString(text, width() / 2, height() / 2);
  sprite().setTextDatum(TL_DATUM);  // повертаємо датум за замовчуванням
} */

void Display::setCursor(int32_t x, int32_t y) { dXY(&x, &y); sprite().setCursor(x, y); }

void Display::pushImage(int32_t x, int32_t y, int32_t w, int32_t h, const uint16_t* data) {
  dXY(&x, &y); 
  sprite().pushImage(x, y, w, h, data);
}

void Display::pushImage8bpp(int32_t x, int32_t y, int32_t w, int32_t h, const uint8_t* data) {
  dXY(&x, &y);
#if defined(BOARD_ESP32_C6) || defined(BOARD_ESP32_S3_LCD147) || defined(BOARD_TTGO_T1) || defined(BOARD_ST7789)
  // Canvas Arduino_GFX завжди 16-біт RGB565 - конвертуємо RGB332 -> RGB565
  // РЯДОК ЗА РЯДКОМ у невеликий тимчасовий буфер (w * 2 байти), а не в один
  // повний w*h*2 буфер: плата без PSRAM, зайві ~100+ KB на кадр тут дорогі.
  uint16_t* rowBuffer = static_cast<uint16_t*>(malloc((size_t)w * sizeof(uint16_t)));
  if (rowBuffer == nullptr) {
    _logger.error("pushImage8bpp: не вдалось виділити рядковий буфер (%d px)", (int)w);
    return;
  }
  for (int32_t row = 0; row < h; row++) {
    const uint8_t* srcRow = data + (size_t)row * w;
    for (int32_t col = 0; col < w; col++) {
      rowBuffer[col] = rgb332to565(srcRow[col]);
    }
    sprite().pushImage(x, y + row, w, 1, rowBuffer);
  }
  free(rowBuffer);
#elif !defined(BOARD_4848S040) && !defined(BOARD_ESP8266)
  // Справжній bodmer/TFT_eSPI (esp32-st7789, ttgo-t1) має pushImage(...,uint8_t*,bool,uint16_t*).
  // bpp8=true -> дані трактуються як "рідний" 8bpp формат сприту (RGB332), без палітри.

  sprite().pushImage(x, y, w, h, const_cast<uint8_t*>(data), true);
  // sprite().pushImage(x, y, w, h, const_cast<uint8_t*>(data));
  // sprite().pushImage(x, y, w, h, (const uint16_t*)(data)); // esp32-s3-lcd147
#else
  // LGFX (4848s040), SSD1306-шим (esp8266) не мають сумісного 8bpp pushImage -
  // на цих платах SPRITE_COLOR_DEPTH=8 для фонових зображень не використовується
  // (див. platformio.ini, JpegColorDepth у main.cpp).
  _logger.error("pushImage8bpp() не підтримується на цій платі");
#endif
}

void Display::flush() { 
#if defined(DISPLAY_SPLIT_COUNT) && DISPLAY_SPLIT_COUNT
  int x = 0, y = 0;
  y = y + (_activeSplitBlock * (height() / DISPLAY_SPLIT_COUNT));
  sprite().pushSprite(x, y);
#endif
  // unbuffered режим: pushImage()/drawX() і так пишуть напряму в tft_,
  // немає накопиченого кадру, який треба "вивести" — no-op.
}

int Display::width() const { return width_; }

int Display::height() const { return height_; }

void Display::brightness(uint8_t percent) {
  percent = percent < 0 ? 0 : percent;
  percent = percent > 100 ? 100 : percent;

#if defined(TFT_BL)
  analogWrite(TFT_BL, map(percent, 0, 100, 0, 255));
#endif

#if defined(BOARD_4848S040)
// tft_.setBrightness(map(percent, 0, 100, 10, 255)); // делегуємо в LGFX Light_PWM, пін вже
// сконфігурований у Setup_ST7701_4848S040.h
#endif

#if defined(BOARD_ESP8266)
  // SSD1306 не має підсвітки - єдина доступна ручка яскравості це контраст пікселів (0..255)
  // tft_.ssd1306_command(SSD1306_SETCONTRAST);
  // tft_.ssd1306_command(map(percent, 0, 100, 0, 255));

  Wire.beginTransmission(0x3C);
  Wire.write(0x00);  // command mode
  Wire.write(0x81);  // SETCONTRAST
  Wire.write(map(percent, 0, 100, 0, 255));
  Wire.endTransmission();
#endif

  brightness_ = percent;
}

const uint32_t Display::loopFrameRate() {
  // --- Метрики "здоров'я" системи ---
  static uint32_t loopCounter = 0;
  static uint32_t loopsPerSecond = 0;
  static uint32_t lastLoopCheckMs = 0;

  loopCounter++;

  uint32_t now = millis();

  // Підрахунок швидкості циклів loop() за секунду
  if (now - lastLoopCheckMs >= 1000) {
    loopsPerSecond = loopCounter;
    loopCounter = 0;
    lastLoopCheckMs = now;
  }

  return loopsPerSecond;
}
