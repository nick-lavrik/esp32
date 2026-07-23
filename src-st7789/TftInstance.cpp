// TftInstance_ST7789.cpp
// Компілюється ЛИШЕ в env:esp32-st7789 (виключається через build_src_filter
// у платах, де LovyanGFX, — дивись platformio.ini).
#include "TftInstance.h"

TFT_eSPI tft = TFT_eSPI();
