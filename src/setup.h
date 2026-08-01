#pragma once

#include <Arduino.h>
#include "Display.h"

extern Display display;

void setupSerial() {
    Serial.begin(115200);
    delay(200);
    Serial.printf("\n\n-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*\n");
    Serial.printf(" %s (%s)\n", ESP.getChipModel(), PIO_PIOENV );
    Serial.printf("-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*\n\n");
}

void setupDisplay() {
    #if defined(BOARD_ST7789)
    pinMode(TFT_BL, OUTPUT); // st7789
    #endif

    display.init();
    //display.autobrightness(true);
    Serial.println("Display setup done.");
}
