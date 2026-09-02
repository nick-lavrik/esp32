#pragma once

#include <stdint.h>

// Геометрія сцени в пікселях КОНКРЕТНОГО екрана - єдиний міст «дисплей -> гра».
// Заповнює рендер (він знає і розмір екрана, і розміри спрайтів), модель лише
// читає. Завдяки цьому lib/DinoGame не знає ні про Display, ні про асети.
struct DinoLayout {
  int16_t viewW = 0;
  int16_t viewH = 0;

  int16_t groundY = 0;  // y лінії горизонту: низ усіх спрайтів стоїть на ній
  int16_t playerX = 0;  // діно нерухомий по X - рухається світ
  int16_t playerW = 0;
  int16_t playerH = 0;

  static constexpr uint8_t kMaxKinds = 4;
  int16_t obstacleW[kMaxKinds] = {};
  int16_t obstacleH[kMaxKinds] = {};
  uint8_t obstacleCount = 0;

  // Висота стрибка в пікселях. Рендер обмежує її вільним місцем під HUD,
  // тому модель бере вже готове число і не гадає про розкладку екрана.
  int16_t jumpApex = 0;

  bool valid() const {
    return viewW > 0 && viewH > 0 && playerW > 0 && playerH > 0 && obstacleCount > 0 && jumpApex > 0;
  }
};
