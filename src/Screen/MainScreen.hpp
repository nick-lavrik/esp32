#pragma once

// Основний екран: фон, системна інформація, годинник (опційно), накладки
// cron-тасків (іконка WiFi, debug-рамка дотику).
//
// Кнопка: клік - годинник on/off (із затримкою на вікно double-click);
// 3 с - погасити екран / повернути попередню яскравість; double-click -
// наступний екран (Screen::onButton()). Тач: свайп угору/вниз - яскравість +-10%; утримання -
// debug-рамка зон дотику і (з датчиком освітленості) автояскравість.

#include "Screen.hpp"

class MainScreen : public Screen {
public:
  const char* name() const override { return "main"; }
  void drawStrip(bool frameStart) override;
  bool overlays() const override { return true; }

  void onButton(ButtonId id, ButtonEvent event, uint32_t nowMs, uint32_t heldMs) override;
  void onHold(TouchPoint p, unsigned long heldMs) override;
  void onSwipe(SwipeDirection dir, TouchPoint start, TouchPoint end) override;

private:
  // Що повернути після "погасити екран" довгим утриманням кнопки.
  uint8_t _savedBrightness = 0;
  bool _savedAutoBrightness = false;
};
