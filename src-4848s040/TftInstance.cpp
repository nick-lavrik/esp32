// TftInstance_4848S040.cpp
// Компілюється ЛИШЕ в env:esp32-4848s040 (виключається у ST7789-збірці
// через build_src_filter — дивись platformio.ini).
#include "TftInstance.h"

TFT_eSPI tft; // тут TFT_eSPI — це alias на клас LGFX із Setup_ST7701_4848S040.h
