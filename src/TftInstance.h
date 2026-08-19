#pragma once

// Вибір реалізації відбувається через -D у build_flags конкретного env
// (не через -include!), щоб <LovyanGFX.hpp> підключався ЛИШЕ у файлах,
// які реально його потребують, а не в кожному .cpp проєкту та бібліотек.
//
//   env:esp32-st7789      -> нічого додаткового не треба (за замовчуванням)
//   env:esp32-4848s040    -> build_flags: -DBOARD_4848S040
//   env:esp8266           -> build_flags: -DBOARD_ESP8266
//   env:esp32-c6           -> build_flags: -DBOARD_ESP32_C6
//   env:esp32-c6-lcd096    -> build_flags: -DBOARD_ESP32_C6_LCD096

#if defined(BOARD_4848S040)
#include "Setup_ST7701_4848S040.h"  // визначає клас LGFX + alias TFT_eSPI
#elif defined(BOARD_ESP8266)
#include "Setup_SSD1306_NodeMCU.h"  // TFT_eSPI/TFT_eSprite-сумісна обгортка над Adafruit_SSD1306
#elif defined(BOARD_ESP32_C6)
#include "Setup_JD9853_C6.h"  // TFT_eSPI/TFT_eSprite-сумісна обгортка над Arduino_GFX (JD9853)
#elif defined(BOARD_ESP32_C6_LCD096)
#include "Setup_ST7735_C6_LCD096.h"  // TFT_eSPI/TFT_eSprite-сумісна обгортка над Arduino_GFX (ST7735S)
#else
#include <TFT_eSPI.h>  // справжній bodmer/TFT_eSPI (ST7789, SPI)
#endif

/**
 * єдиний глобальний екземпляр, визначений в одному з
 * @see file://./../src-4848s040/TftInstance.cpp
 * @see file://./../src-st7789/TftInstance.cpp
 * @see file://./../src-esp8266/TftInstance.cpp
 * @see file://./../src-esp32-c6/TftInstance.cpp
 * @see file://./../src-esp32-c6-lcd096/TftInstance.cpp
 */
extern TFT_eSPI tft;
