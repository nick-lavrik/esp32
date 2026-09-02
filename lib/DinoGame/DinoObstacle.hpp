#pragma once

#include <stdint.h>

// Одна перешкода. kind - ІНДЕКС у DinoLayout::obstacleW/obstacleH, а не enum:
// саме тому додати птеродактиля згодом означатиме додати запис у layout
// (з ненульовою висотою польоту), а не правити цю структуру й усі switch'і.
struct DinoObstacle {
  float x = 0.0f;    // ліва межа, px від лівого краю екрана
  uint8_t kind = 0;  // індекс у таблиці розмірів DinoLayout
  bool active = false;
};
