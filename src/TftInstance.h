#pragma once

// Вибір реалізації відбувається через -D у build_flags конкретного env
// (не через -include!), щоб <LovyanGFX.hpp> підключався ЛИШЕ у файлах,
// які реально його потребують, а не в кожному .cpp проєкту та бібліотек.
//
//   env:esp32-st7789      -> нічого додаткового не треба (за замовчуванням)
//   env:esp32-4848s040    -> build_flags: -DBOARD_4848S040

#if defined(BOARD_4848S040)
  #include "Setup_ST7701_4848S040.h" // визначає клас LGFX + alias TFT_eSPI
#else
  #include <TFT_eSPI.h>              // справжній bodmer/TFT_eSPI (ST7789, SPI)
#endif

/**
 * єдиний глобальний екземпляр, визначений в одному з
 * @see file://./../src-4848s040/TftInstance.cpp
 * @see file://./../src-st7789/TftInstance.cpp
 */
extern TFT_eSPI tft; 
