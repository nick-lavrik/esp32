// setup.h
#pragma once

#include <Arduino.h>
#include "Display.hpp"

extern Display display;

void setupSerial() {
    Serial.begin(115200);
    delay(200);
    Serial.printf("\n\n-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*\n");
    // ESP8266 не має ESP.getChipModel() (це ESP32 API) - виводимо ChipId замість нього
    Serial.printf(" ESP8266 (chipId=0x%06X) (%s)\n", ESP.getChipId(), PIO_PIOENV);
    Serial.printf("-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*\n\n");
}

void setupDisplay() {
    display.init();
    Serial.println("Display setup done.");
}
