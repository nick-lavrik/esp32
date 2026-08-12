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

const uint16_t* getBackgroundImage(uint16_t* width, uint16_t* height) {
  static size_t currentIndex = 0;

  if (_activeInstance != nullptr && _activeInstance->isLoaded()) {
    *width = _activeInstance->width();
    *height = _activeInstance->height();
    return static_cast<uint16_t*>(_activeInstance->buffer());
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

  *width = 320;
  *height = 240;
  return backgroundImages[currentIndex];
}

extern Display display;
void drawBackgroundImage() {
  uint16_t width, height;
  const uint16_t* ptr = getBackgroundImage(&width, &height);

  if (ptr) {
    display.pushImage((uint32_t)(display.width() - width) / 2,
                      (uint32_t)(display.height() - height) / 2, width, height, ptr);
  } else {
    display.clear();
  }
}
