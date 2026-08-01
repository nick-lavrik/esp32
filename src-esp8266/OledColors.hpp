// OledColors.hpp
#pragma once

// SSD1306 - монохромний дисплей (лише 2 стани пікселя: увімкнено/вимкнено).
// TFT_eSPI-код (wifi.h/ntp.h) оперує кольоровими константами TFT_XXX -
// тут вони усі мапляться в "увімкнений піксель" (WHITE), окрім TFT_BLACK.
// Розрізнення кольорів (напр. TFT_RED для помилки) на монохромному екрані
// неможливе - лишається лише сам факт малювання тексту.
#define TFT_BLACK     0
#define TFT_WHITE     1
#define TFT_RED       1
#define TFT_GREEN     1
#define TFT_YELLOW    1
#define TFT_CYAN      1
#define TFT_ORANGE    1
#define TFT_LIGHTGREY 1
#define TFT_DARKGREY  1
#define TFT_TRANSPARENT 0
