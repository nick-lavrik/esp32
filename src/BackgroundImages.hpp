#pragma once

#include <cstddef>
#include <cstdint>

#include "Display.h"
#include "JpegImage.hpp"

// Самі дані зображень (76800 = 240*320 пікселів, RGB565) визначені
// в іншому місці проєкту — тут лише оголошуємо їх як зовнішні,
// щоб зібрати в один масив вказівників.
extern const uint16_t backgroundSpace01[76800];
extern const uint16_t backgroundSpace02[76800];
extern const uint16_t backgroundSpace03[76800];

// Кількість фонових зображень
#if defined(BOARD_ST7789) && !defined(BACKGROUND_IMAGES_COUNT)
#define BACKGROUND_IMAGES_COUNT 1
#endif

#if defined(BOARD_4848S040) && !defined(BACKGROUND_IMAGES_COUNT)
#define BACKGROUND_IMAGES_COUNT 1
#endif

#if !defined(BACKGROUND_IMAGES_COUNT)
#define BACKGROUND_IMAGES_COUNT 0
#endif

// Масив вказівників на всі фонові зображення (індекси 0..2)
extern const uint16_t* const backgroundImages[BACKGROUND_IMAGES_COUNT];

// Повертає поточне зображення, циклічно. bufferIs8bpp виставляється в true,
// якщо повернений буфер - RGB332 (uint8_t*, 1 байт/піксель), інакше RGB565.
const void* getBackgroundImage(uint16_t* width, uint16_t* height, bool* bufferIs8bpp);
void drawBackgroundImage();
void setBackgroundImage(JpegImage& image);
