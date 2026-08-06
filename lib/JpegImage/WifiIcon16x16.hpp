#pragma once

#include <MonoBitmap.hpp>

// Статична 1bpp іконка Wi-Fi (16x16), стандартний вигляд: 3 концентричні дуги + крапка.
// Формат даних сумісний з MonoBitmap / Adafruit_GFX::drawBitmap() (MSB-first, rowStride = 2 байти).
//
// Використання:
//   #include "WifiIcon16x16.hpp"
//   WifiIcon16x16::bitmap().draw(tft, x, y, SSD1306_WHITE);
class WifiIcon16x16 {
public:
  static const MonoBitmap &bitmap();

  static constexpr uint16_t kWidth = 16;
  static constexpr uint16_t kHeight = 16;

private:
  static const uint8_t kData[32];
};
