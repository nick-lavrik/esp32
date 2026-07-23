// BackgroundImages.h
#pragma once

#include <cstddef>
#include <cstdint>

// Самі дані зображень (76800 = 240*320 пікселів, RGB565) визначені
// в іншому місці проєкту — тут лише оголошуємо їх як зовнішні,
// щоб зібрати в один масив вказівників.
extern const uint16_t backgroundSpace01[76800];
extern const uint16_t backgroundSpace02[76800];
extern const uint16_t backgroundSpace03[76800];

// Кількість фонових зображень
constexpr size_t BACKGROUND_IMAGES_COUNT = 3;

// Масив вказівників на всі фонові зображення (індекси 0..2)
extern const uint16_t* const backgroundImages[BACKGROUND_IMAGES_COUNT];

// Повертає індекс наступного зображення, циклічно
// (напр. nextBackgroundIndex(2) поверне 0)
size_t nextBackgroundIndex(size_t currentIndex);

