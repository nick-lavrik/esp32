// Display.cpp
#include "Display.hpp"

#if HAS_SCREEN_MIRROR
#include <ScreenMirror.hpp>
#endif

void Display::init() {
  driver_.init(TFT_ROTATION);

  width_ = driver_.width();
  height_ = driver_.height();

#if DISPLAY_SPLIT_COUNT > 0
  // Полотно на одну смугу — вся подальша робота йде через нього.
  const int stripH = height_ / DISPLAY_SPLIT_COUNT;
  _logger.info("initSprite(%d, %d, depth=%d)", width_, stripH, SPRITE_COLOR_DEPTH);
  if (!driver_.beginCanvas(width_, stripH, SPRITE_COLOR_DEPTH)) {
    _logger.error("ERROR: createSprite() could not allocate memory!");
    _logger.error("Required: %d bytes, free (heap): %u bytes", width_ * height_ * 2,
                  ESP.getFreeHeap());
  }
#endif

#if HAS_SCREEN_MIRROR
  ScreenMirror::instance().begin((uint16_t)width_, (uint16_t)height_, splitCount(),
                                 (uint16_t)splitHeight(), DisplayDriver::kCanvasSwapped565);
#endif

  flush();  // одразу показуємо чорний кадр, щоб не лишався сміттєвий вміст VRAM
}

void Display::flip() {
  _logger.info("TFT.setRotation(%d)", (driver_.getRotation() + 2) % 4);
  driver_.setRotation((driver_.getRotation() + 2) % 4);
}

void Display::drawText(int x, int y, const char* text, uint16_t color) {
  dXY(&x, &y);
  driver_.setTextColor(color);
  driver_.drawString(text, x, y);
}

void Display::pushImage8bpp(int32_t x, int32_t y, int32_t w, int32_t h, const uint8_t* data) {
  dXY(&x, &y);
  if (!driver_.pushImage8bpp(x, y, w, h, data)) {
    _logger.error("pushImage8bpp() failed (%d px): no line buffer or backend lacks 8bpp", (int)w);
  }
}

void Display::drawBitmapScaled(int32_t x, int32_t y, const uint8_t *bitmap, int32_t w, int32_t h,
                               uint32_t color, uint8_t scale) {
  if (!bitmap || w <= 0 || h <= 0) return;
  if (scale < 1) scale = 1;

  // dXY() рівно один раз: далі все рахується вже в координатах активної смуги.
  dXY(&x, &y);
  const int32_t stripH = splitHeight();
  if (y >= stripH || y + h * (int32_t)scale <= 0) return;  // цілком поза смугою

  if (scale == 1) {
    // Не через public drawBitmap() - той знову прогнав би dXY().
    driver_.drawBitmap((int16_t)x, (int16_t)y, bitmap, (int16_t)w, (int16_t)h, (uint16_t)color);
    return;
  }

  // Формат той самий, що в MonoBitmap / Adafruit_GFX::drawBitmap():
  // 1 біт/піксель, MSB = лівий піксель рядка, рядок доповнений до цілого байта.
  const int32_t stride = (w + 7) / 8;

  for (int32_t row = 0; row < h; ++row) {
    const int32_t ry = y + row * (int32_t)scale;
    if (ry + (int32_t)scale <= 0) continue;  // рядок ще вище смуги
    if (ry >= stripH) break;                 // далі тільки нижче - виходимо

    const uint8_t *p = bitmap + row * stride;
    int32_t col = 0;
    while (col < w) {
      // Малюємо ПРОГОНАМИ однакових бітів: один fillRect замість scale*scale
      // викликів drawPixel. Для типового спрайта це 3-4 виклики на рядок.
      while (col < w && !(pgm_read_byte(p + (col >> 3)) & (0x80 >> (col & 7)))) ++col;
      if (col >= w) break;
      int32_t run = col;
      while (run < w && (pgm_read_byte(p + (run >> 3)) & (0x80 >> (run & 7)))) ++run;
      driver_.fillRect(x + col * scale, ry, (run - col) * scale, scale, color);
      col = run;
    }
  }
}

void Display::flush() {
#if DISPLAY_SPLIT_COUNT > 0
  driver_.pushCanvas(_activeSplitBlock * (height() / DISPLAY_SPLIT_COUNT));
#if HAS_SCREEN_MIRROR
  // Рівно та сама смуга, що поїхала в панель - але лише якщо її хтось просив.
  // У звичайному режимі (вкладку Screen ніхто не відкрив) це один atomic load.
  ScreenMirror::instance().capture(driver_.canvasPixels(), (uint8_t)_activeSplitBlock);
#endif
#endif
  // unbuffered режим: pushImage()/drawX() і так пишуть напряму в панель,
  // немає накопиченого кадру, який треба "вивести" — no-op.
}

void Display::brightness(uint8_t percent) {
  // Нижньої межі не перевіряємо: percent - uint8_t, тобто "percent < 0"
  // (як було раніше) завжди false і clamp був мертвим кодом.
  percent = percent > 100 ? 100 : percent;
  driver_.setBrightness(percent);
  brightness_ = percent;
}

uint32_t Display::loopFrameRate() {
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
