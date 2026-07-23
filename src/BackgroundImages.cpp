// BackgroundImages.cpp
#include <Arduino.h>
#include "BackgroundImages.h"
#include "Display.h"

#include "../assets/space-01.h"
#include "../assets/space-02.h"
#include "../assets/space-03.h"

const uint16_t* const backgroundImages[BACKGROUND_IMAGES_COUNT] PROGMEM = {
    backgroundSpace02,
    #ifdef BOARD_4848S040
    backgroundSpace03,
    backgroundSpace01,
    #endif
};

const uint16_t* getBackgroundImage() {
    static uint32_t lastUpdateMs = 0;
    static size_t currentIndex = 0;
    uint32_t now = millis();

    if (BACKGROUND_IMAGES_COUNT == 0) {
        return nullptr;
    }

    if (now - lastUpdateMs >= 5000) {
        currentIndex = (currentIndex + 1) % BACKGROUND_IMAGES_COUNT;
        lastUpdateMs = now;
    }

    return backgroundImages[currentIndex];
}

extern Display display;
void drawBackgroundImage() {
    if (BACKGROUND_IMAGES_COUNT > 0) {
        display.pushImage((uint32_t) (display.width() - 320) / 2, (uint32_t) (display.height() - 240) / 2, 320, 240, getBackgroundImage());
    }
}
