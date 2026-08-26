// MonoImageExample.cpp
// Демонстрація: виведення картинки на SSD1306 (128x64, монохром)
// двома способами. Викликати showFromMemory()/showFromLittleFS() з main.cpp.

#include <Arduino.h>

#include "Display.h"  // extern TFT_eSPI tft; (Setup_SSD1306_NodeMCU.h)
#include "JpegImage.hpp"
#include "MonoBitmap.hpp"

// Масив згенерований data/convert (mono1-режим), напр.:
//   cd data && gcc convert.c -o convert -lm
//   ./convert space-01.jpg ../assets/space-mono-128x64.h 128 64 mono1 spaceMono128x64 128
// #include "../assets/space-mono-128x64.h"

// --- Варіант 1: картинка з PROGMEM (як backgroundSpace03, але 1bpp) ---
void showFromMemory() {
  /* static const MonoBitmap image(spaceMono128x64, spaceMono128x64Width, spaceMono128x64Height);

  display.clearDisplay();
  image.draw(display, 0, 0, SSD1306_WHITE);
  display.display(); */
}

// --- Варіант 2: картинка з LittleFS (JPEG -> декодування -> 1bpp) ---
// path, наприклад "/space-128x64.jpg" (або з build_flags LITTLEFS_BACKGROUND_IMAGE)
void showFromLittleFS(const char *path) {
  JpegImage img;
  img.setMonoThreshold(128);  // 0-255, підняти якщо картинка виходить надто темною

  if (!img.loadFromLittleFS(path, JpegColorDepth::MONO1)) {
    Serial.printf("[MonoImageExample] Failed to load %s\n", path);
    return;
  }

  tft.clearDisplay();
  tft.drawBitmap(0, 0, img.bufferMono1(), img.width(), img.height(), SSD1306_WHITE);
  tft.display();
}
