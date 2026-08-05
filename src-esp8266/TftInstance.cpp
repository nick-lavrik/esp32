// TftInstance.cpp
// Компілюється ЛИШЕ в env:esp8266
#include "TftInstance.h"

TFT_eSPI tft = TFT_eSPI();  // тут TFT_eSPI — це alias на клас-обгортку над Adafruit_SSD1306 з
                            // Setup_SSD1306_NodeMCU.h
