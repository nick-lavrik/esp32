// TftEspiDriver.cpp
#include "DisplayDriver.hpp"  // не TftEspiDriver.hpp: спершу потрібен бекенд (TFT_eSPI/TFT_eSprite)

#include <stdlib.h>  // malloc/free для рядкового буфера в pushImage8bpp

#include "Rgb332.hpp"

void TftEspiDriver::init(uint8_t rotation) {
  panel_.init();
  panel_.setRotation(rotation);

  // Регістр 0x36 керує відображенням.
  // Біти MX (6-й) та MY (7-й) відповідають за дзеркальність по X та Y.
  // panel_.writeCommand(0x36);
  // panel_.writeData(0); // Стандарт
  // gfx->writeData(0x40); // Дзеркало по горизонталі (MX=1)
  // gfx->writeData(0x80); // Дзеркало по вертикалі (MY=1)
  // gfx->writeData(0xC0); // Обидва (MX=1, MY=1)

#if DISPLAY_SPLIT_COUNT == 0
  // При буфері swap-байти ставить beginCanvas() на сам спрайт.
  panel_.setSwapBytes(true);
#endif
}

#if DISPLAY_SPLIT_COUNT > 0
bool TftEspiDriver::beginCanvas(int16_t w, int16_t h, uint8_t colorDepth) {
  canvas_.setColorDepth(colorDepth);
  void* buf = canvas_.createSprite(w, h);
  canvas_.fillSprite(TFT_BLACK);
  canvas_.setSwapBytes(true);
  return buf != nullptr;
}
#endif

bool TftEspiDriver::pushImage8bpp(int32_t x, int32_t y, int32_t w, int32_t h, const uint8_t* data) {
#if defined(BOARD_ESP32_C6) || defined(BOARD_ESP32_C6_LCD096) || defined(BOARD_ESP32_S3_LCD147) || defined(BOARD_TTGO_T1) || defined(BOARD_ST7789)
  // Canvas Arduino_GFX завжди 16-біт RGB565 - конвертуємо RGB332 -> RGB565
  // РЯДОК ЗА РЯДКОМ у невеликий тимчасовий буфер (w * 2 байти), а не в один
  // повний w*h*2 буфер: плата без PSRAM, зайві ~100+ KB на кадр тут дорогі.
  //
  // Буфер переюзний (член класу), а не локальний malloc/free: цей метод
  // викликається з drawBackgroundImage() ЩОКАДРУ, і пара malloc/free на кадр
  // - зайвий тиск на купу й привід для її фрагментації на платі без PSRAM.
  if (rowBufferPx_ < w) {
    free(rowBuffer_);
    rowBuffer_ = static_cast<uint16_t*>(malloc((size_t)w * sizeof(uint16_t)));
    rowBufferPx_ = (rowBuffer_ != nullptr) ? w : 0;
  }
  if (rowBuffer_ == nullptr) return false;
  for (int32_t row = 0; row < h; row++) {
    const uint8_t* srcRow = data + (size_t)row * w;
    for (int32_t col = 0; col < w; col++) {
      rowBuffer_[col] = rgb332to565(srcRow[col]);
    }
    target().pushImage(x, y + row, w, 1, rowBuffer_);
  }
  return true;
#elif !defined(BOARD_4848S040) && !defined(BOARD_ESP8266)
  // Справжній bodmer/TFT_eSPI (esp32-st7789, ttgo-t1) має pushImage(...,uint8_t*,bool,uint16_t*).
  // bpp8=true -> дані трактуються як "рідний" 8bpp формат сприту (RGB332), без палітри.

  target().pushImage(x, y, w, h, const_cast<uint8_t*>(data), true);
  // target().pushImage(x, y, w, h, const_cast<uint8_t*>(data));
  // target().pushImage(x, y, w, h, (const uint16_t*)(data)); // esp32-s3-lcd147
  return true;
#else
  // LGFX (4848s040), SSD1306-шим (esp8266) не мають сумісного 8bpp pushImage -
  // на цих платах SPRITE_COLOR_DEPTH=8 для фонових зображень не використовується
  // (див. platformio.ini, JpegColorDepth у main.cpp).
  (void)x; (void)y; (void)w; (void)h; (void)data;
  return false;
#endif
}

void TftEspiDriver::setBrightness(uint8_t percent) {
#if defined(TFT_BL)
  analogWrite(TFT_BL, map(percent, 0, 100, 0, 255));
#endif

#if defined(BOARD_4848S040)
// panel_.setBrightness(map(percent, 0, 100, 10, 255)); // делегуємо в LGFX Light_PWM, пін вже
// сконфігурований у Setup_ST7701_4848S040.h
#endif

#if defined(BOARD_ESP8266)
  // SSD1306 не має підсвітки - єдина доступна ручка яскравості це контраст пікселів (0..255)
  // panel_.ssd1306_command(SSD1306_SETCONTRAST);
  // panel_.ssd1306_command(map(percent, 0, 100, 0, 255));

  Wire.beginTransmission(0x3C);
  Wire.write(0x00);  // command mode
  Wire.write(0x81);  // SETCONTRAST
  Wire.write(map(percent, 0, 100, 0, 255));
  Wire.endTransmission();
#endif

  (void)percent;  // на платах без TFT_BL і без SSD1306 яскравість не керується
}
