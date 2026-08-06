#pragma once

#include <MonoBitmap.hpp>

// Статична 1bpp іконка Wi-Fi (16x16), стандартний вигляд: 3 концентричні дуги + крапка.
// Формат даних сумісний з MonoBitmap / Adafruit_GFX::drawBitmap() (MSB-first, rowStride = 2 байти).
//
// Використання:
//   #include "MonoIcon16x16.hpp"
//   MonoIcon16x16::wifi().draw(tft, x, y, SSD1306_WHITE);
//   MonoIcon16x16::empty().draw(tft, x, y, SSD1306_WHITE);
class MonoIcon16x16 {
public:
  static const MonoBitmap &wifi();
  static const MonoBitmap &empty();

  static constexpr uint16_t kWidth = 16;
  static constexpr uint16_t kHeight = 16;

private:
  static const uint8_t kDataWifi[32]; // 16b x 16r => 2B x 16r => 32B
  static const uint8_t kDataEmpty[32];
};
