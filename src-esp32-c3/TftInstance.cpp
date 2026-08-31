// TftInstance.cpp
// Компілюється ЛИШЕ в env:esp32-c3 (плата без дисплея).
//
// Сам об'єкт - заглушка з include/Setup_Headless.h: усі методи порожні
// й inline, тобто в прошивці від нього лишається один байт стану
// (_rotation) і жодного коду.
#include "TftInstance.h"

TFT_eSPI tft = TFT_eSPI();
