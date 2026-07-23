// BackgroundImages.h
#pragma once

#include <cstddef>
#include <cstdint>

#include "Display.h"

// Самі дані зображень (76800 = 240*320 пікселів, RGB565) визначені
// в іншому місці проєкту — тут лише оголошуємо їх як зовнішні,
// щоб зібрати в один масив вказівників.
extern const uint16_t backgroundSpace01[76800];
extern const uint16_t backgroundSpace02[76800];
extern const uint16_t backgroundSpace03[76800];

// Кількість фонових зображень
#ifdef BOARD_ST7789
constexpr size_t BACKGROUND_IMAGES_COUNT = 1;
#elifdef BOARD_4848S040
constexpr size_t BACKGROUND_IMAGES_COUNT = 3;
#endif

// Масив вказівників на всі фонові зображення (індекси 0..2)
extern const uint16_t* const backgroundImages[BACKGROUND_IMAGES_COUNT];

// Повертає поточне зображення, циклічно
const uint16_t* getBackgroundImage();
void drawBackgroundImage();
