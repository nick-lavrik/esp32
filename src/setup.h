#pragma once

#include <Arduino.h>
#include "Display.h"

extern Display display;

void setupSerial() {
    Serial.begin(115200);
    delay(200);
    Serial.printf("\n\n-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*\n");
    #if defined(BOARD_ESP8266)
    // ESP8266 не має ESP.getChipModel() (це ESP32 API) - виводимо ChipId замість нього
    Serial.printf(" ESP8266 (chipId=0x%06X) (%s)\n", ESP.getChipId(), PIO_PIOENV);
    #else
    Serial.printf(" %s (%s)\n", ESP.getChipModel(), PIO_PIOENV );
    #endif
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
