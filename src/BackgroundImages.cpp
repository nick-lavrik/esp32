// BackgroundImages.cpp
#include <Arduino.h>

#include <BackgroundImages.hpp>

#include "Display.h"

#if BACKGROUND_IMAGES_COUNT >= 1
#include "../assets/background-01-320x240.h"
#endif

#if BACKGROUND_IMAGES_COUNT >= 2
#include "../assets/background-02-320x240.h"
#endif

#if BACKGROUND_IMAGES_COUNT >= 3
#include "../assets/background-03-320x240.h"
#endif

#if BACKGROUND_IMAGES_COUNT >= 4
#include "../assets/background-04-320x240.h"
#endif

static JpegImage* _activeInstance = nullptr;

void setBackgroundImage(JpegImage& image) { _activeInstance = &image; }

const uint16_t* const backgroundImages[BACKGROUND_IMAGES_COUNT] PROGMEM = {
#if BACKGROUND_IMAGES_COUNT >= 1
    background_01,
#endif
#if BACKGROUND_IMAGES_COUNT >= 2
    background_02,
#endif
#if BACKGROUND_IMAGES_COUNT >= 3
    background_03,
#endif
#if BACKGROUND_IMAGES_COUNT >= 4
    background_04,
#endif
};

const void* getBackgroundImage(uint16_t* width, uint16_t* height, bool* bufferIs8bpp) {
  static size_t currentIndex = 0;

  *bufferIs8bpp = false;

  if (_activeInstance != nullptr && _activeInstance->isLoaded()) {
    *width = _activeInstance->width();
    *height = _activeInstance->height();
    *bufferIs8bpp = (_activeInstance->colorDepth() == JpegColorDepth::RGB332);
    return _activeInstance->buffer();
  }

  if (_activeInstance != nullptr) {
    return nullptr;
  }

#if BACKGROUND_IMAGES_COUNT == 0
  return nullptr;
#endif

#if BACKGROUND_IMAGES_COUNT > 0
  uint32_t now = millis();
  static uint32_t lastUpdateMs = 0;
  if (now - lastUpdateMs >= 5000) {
    currentIndex = (currentIndex + 1) % BACKGROUND_IMAGES_COUNT;
    lastUpdateMs = now;
  }
#endif

  // Вбудовані background_XX-асети завжди зашиті як RGB565 (див. BackgroundImages.hpp)
  *width = 320;
  *height = 240;
  return backgroundImages[currentIndex];
}

extern Display display;
void drawBackgroundImage() {
  uint16_t width, height;
  bool is8bpp = false;
  const void* ptr = getBackgroundImage(&width, &height, &is8bpp);

  if (ptr) {
    int32_t x = (int32_t)(display.width() - width) / 2;
    int32_t y = (int32_t)(display.height() - height) / 2;
    if (is8bpp) {
      display.pushImage8bpp(x, y, width, height, static_cast<const uint8_t*>(ptr));
    } else {
      display.pushImage(x, y, width, height, static_cast<const uint16_t*>(ptr));
    }
  } else {
    display.clear();
  }
}
