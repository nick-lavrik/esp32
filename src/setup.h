#pragma once

#include <Arduino.h>

#include <Logger.hpp>
#include <RwLock.hpp>

#include "Display.h"

extern Display display;

#include <ScreenLogTail.hpp>

void setupSerial() {
  // #if !defined(SCREEN_LOG_TAIL_LINES) || !SCREEN_LOG_TAIL_LINES
  rwlock::registerObject(Serial);
  rwlock::registerObject(screenLogTail());
  // #endif

  Serial.begin(115200);
#if !defined(BOARD_ESP8266)
  // Native USB CDC (HWCDC на ESP32-C3/C6/H2, USBCDC на ESP32-S2/S3) може
  // ЗАВИСНУТИ НАЗАВЖДИ в Serial.print()/flush(), якщо хост тимчасово не
  // встигає вичитувати TX-буфер (відомий баг arduino-esp32, issue #9172:
  // "ESP32-C3 in USB CDC mode can hang if Serial.flush() called"). Оскільки
  // loop() виконується в тому самому таску, що й увесь застосунок,
  // зависання Serial - це зависання ВСЬОЇ прошивки (watchdog зрештою або
  // ресетить пристрій, або таск просто затикається назавжди - саме
  // симптом "консоль зависає, kill -9 не допомагає"). setTxTimeoutMs(0)
  // - офіційний фікс: TX стає неблокуючим, зайві байти відкидаються
  // замість очікування хоста. ESP8266 (класичний UART, не native USB)
  // цього методу не має.
  Serial.setTxTimeoutMs(0);
#endif
  delay(200);
  Logger::info("");
  Logger::info("");
  Logger::info("*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*");
#if defined(BOARD_ESP8266)
  // ESP8266 не має ESP.getChipModel() (це ESP32 API) - виводимо ChipId замість нього
  Logger::info(" ESP8266 (chipId=0x%06X) (%s)", ESP.getChipId(), PIO_PIOENV);
#else
  Logger::info(" %s (%s)", ESP.getChipModel(), PIO_PIOENV);
#endif
  Logger::info("*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*");
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
