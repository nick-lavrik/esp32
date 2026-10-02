#pragma once

// DisplayDriver - те, що Display отримує в конструкторі: шар, який ховає
// конкретну графічну бібліотеку. Display сам не знає ні TFT_eSPI, ні BOARD_*,
// лише контракт драйвера (див. TftEspiDriver.hpp - перелік методів).
//
// Вибір на етапі компіляції (не virtual): один драйвер на прошивку, а
// fillRect()/drawBitmap() кличуться сотні разів на кадр - vtable на кожен
// примітив тут нічого не дає, крім ціни.
//
// Бекенд обирається через -D у build_flags конкретного env (не через
// -include!), щоб <LovyanGFX.hpp> підключався ЛИШЕ у файлах, які реально
// його потребують, а не в кожному .cpp проєкту та бібліотек.
//
//   env:esp32-st7789      -> нічого додаткового не треба (за замовчуванням)
//   env:esp32-4848s040    -> build_flags: -DBOARD_4848S040
//   env:esp8266           -> build_flags: -DBOARD_ESP8266
//   env:esp32-c6           -> build_flags: -DBOARD_ESP32_C6
//   env:esp32-c6-lcd096    -> build_flags: -DBOARD_ESP32_C6_LCD096
//   env:esp32-c3           -> build_flags: -DBOARD_HAS_DISPLAY=0 (дисплея немає)
//
// BOARD_HAS_DISPLAY=0 перевіряється ПЕРШИМ і перекриває будь-який BOARD_*:
// на такій платі підключається заглушка, і жодна графічна бібліотека
// (TFT_eSPI / Arduino_GFX / LovyanGFX) у lib_deps не потрібна взагалі.
//
// Зараз драйвер один - TftEspiDriver, над контрактом TFT_eSPI/TFT_eSprite:
// усі бекенди, крім справжнього bodmer/TFT_eSPI, приведені до цього контракту
// фасадами include/Setup_*.h. Власний драйвер на бекенд (без фасаду) -
// docs/tech_debt.md, розділ 3; тоді гілка нижче отримає свій драйвер замість
// спільного хвоста.
//
// Бібліотеку бекенду в кожній гілці названо ще й тут, а не лише всередині
// Setup_*.h: LDF шукає залежності бібліотеки в її власних файлах і за
// #include у include/ не йде - без цього рядка lib/Display збирався б без
// Arduino_GFX/LovyanGFX/Adafruit у include path ("No such file").

#if defined(BOARD_HAS_DISPLAY) && !BOARD_HAS_DISPLAY
#include "Setup_Headless.h"  // no-op заглушка з API TFT_eSPI (плата без дисплея)
#elif defined(BOARD_4848S040)
#include <LovyanGFX.hpp>
#include "Setup_ST7701_4848S040.h"  // визначає клас LGFX + alias TFT_eSPI
#elif defined(BOARD_ESP8266)
#include <Adafruit_SSD1306.h>
#include "Setup_SSD1306_NodeMCU.h"  // TFT_eSPI/TFT_eSprite-сумісна обгортка над Adafruit_SSD1306
#elif defined(BOARD_ESP32_C6)
#include <Arduino_GFX_Library.h>
#include "Setup_JD9853_C6.h"  // TFT_eSPI/TFT_eSprite-сумісна обгортка над Arduino_GFX (JD9853)
#elif defined(BOARD_ESP32_C6_LCD096)
#include <Arduino_GFX_Library.h>
#include "Setup_ST7735_C6_LCD096.h"  // TFT_eSPI/TFT_eSprite-сумісна обгортка над Arduino_GFX (ST7735S)
#else
#include <TFT_eSPI.h>  // справжній bodmer/TFT_eSPI (ST7789, SPI)
#endif

#include "TftEspiDriver.hpp"
using DisplayDriver = TftEspiDriver;
