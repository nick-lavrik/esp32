#pragma once

// Основний екран: фон, системна інформація, годинник (опційно), накладки
// cron-тасків (іконка WiFi, debug-рамка дотику).
//
// Кнопка: коротко (< 1 с) - годинник on/off; 3 с - погасити екран / повернути
// попередню яскравість. Тач: свайп угору/вниз - яскравість +-10%; утримання -
// debug-рамка зон дотику і (з датчиком освітленості) автояскравість.

#include "Screen.hpp"

class MainScreen : public Screen {
public:
  const char* name() const override { return "main"; }
  void drawStrip(bool frameStart) override;
  bool overlays() const override { return true; }

  void onButtonRelease(uint32_t nowMs, uint32_t heldMs) override;
  void onButtonLongPress(uint32_t nowMs) override;
  void onHold(TouchPoint p, unsigned long heldMs) override;
  void onSwipe(SwipeDirection dir, TouchPoint start, TouchPoint end) override;

private:
  // Що повернути після "погасити екран" довгим утриманням кнопки.
  uint8_t _savedBrightness = 0;
  bool _savedAutoBrightness = false;
};

// Малювання основного екрана - поки в src/main.cpp (тягнуть ping.h/wifi.h,
// що ще не окремі TU; рефакторинг main.cpp, крок 4).
void drawSystemInfo();
void drawTime();
