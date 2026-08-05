#pragma once

#include <Arduino.h>

#include <Logger.hpp>
#include <RwLock.hpp>

#include "Display.h"

extern Display display;

void setupSerial() {
  rwlock::registerObject(Serial);
  Serial.begin(115200);
  delay(200);
  Logger::info("");
  Logger::info("");
  Logger::info("-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*");
#if defined(BOARD_ESP8266)
  // ESP8266 не має ESP.getChipModel() (це ESP32 API) - виводимо ChipId замість нього
  Logger::info(" ESP8266 (chipId=0x%06X) (%s)\n", ESP.getChipId(), PIO_PIOENV);
#else
  Logger::info(" %s (%s)", ESP.getChipModel(), PIO_PIOENV);
#endif
  Logger::info("-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*");
  Logger::info("");
}

void setupDisplay() {
#if defined(BOARD_ST7789)
  pinMode(TFT_BL, OUTPUT);  // st7789
#endif

  display.init();
  // display.autobrightness(true);
  Logger::info("Display setup done.");
}
