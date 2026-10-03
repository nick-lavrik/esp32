#pragma once

// Один режим екрана: основний, гра, тестова таблиця, ... Активний завжди
// рівно один, ним керує ScreenManager (src/Screen/ScreenManager.hpp).
//
//   class ClockScreen : public Screen {
//    public:
//     const char* name() const override { return "clock"; }
//     void drawStrip(bool frameStart) override { /* малює поточну смугу */ }
//   };
//
// Новий екран = новий клас + рядок у setupScreens() (ScreenManager.cpp);
// loop(), кнопку й тач правити не треба - їх події менеджер пересилає сюди.
//
// Контекст виклику - лише loop(): drawStrip() усередині транзакції кадру,
// enter()/leave() і вхідні події - теж з loop() (кнопка - cron-таск,
// тач - TouchController::update()). Малювати можна лише з drawStrip().

#include <Arduino.h>
#include <TouchPoint.h>

enum class SwipeDirection : uint8_t { Up, Down, Left, Right };

class Screen {
public:
  virtual ~Screen() = default;

  // Ім'я для команди 'screen <name>' і логів.
  virtual const char* name() const = 0;

  // false - екран зараз недоступний (напр. рендерер гри не стартував);
  // перемикання на нього менеджер відхилить з попередженням.
  virtual bool available() const { return true; }

  // Перед першою смугою / після останньої. Екран очищує менеджер.
  virtual void enter() {}
  virtual void leave() {}

  // Перед смугою: змінити сцену (напр. наступний патерн). Може викликати
  // screens.invalidate() - менеджер почистить смугу ще до drawStrip().
  virtual void update(bool frameStart) {}

  // Одна смуга кадру (DISPLAY_SPLIT_COUNT смуг на кадр). frameStart - перша
  // смуга нового кадру: змінювати сцену (фізику, патерн) лише тоді, інакше
  // кожна смуга покаже свою фазу.
  virtual void drawStrip(bool frameStart) = 0;

  // true - loop() пропускає блокуючу фонову роботу (doPing(), ecoflow.loop()),
  // яка рве кадр. MQTT і команди працюють і далі.
  virtual bool realtime() const { return false; }

  // true - поверх екрана малюються накладки з cron-тасків (іконка WiFi,
  // debug-рамка дотику).
  virtual bool overlays() const { return false; }

  // Головна кнопка (PRIMARY_BUTTON_PIN). Довге утримання (3 с) за замовчуванням повертає
  // на основний екран - щоб з будь-якого режиму був вихід без консолі.
  virtual void onButtonPress(uint32_t nowMs) {}
  virtual void onButtonRelease(uint32_t nowMs, uint32_t heldMs) {}
  virtual void onButtonLongPress(uint32_t nowMs);

  // Тач (лише BOARD_HAS_TOUCHSCREEN).
  virtual void onTouch(TouchPoint p) {}
  virtual void onRelease(TouchPoint p) {}
  virtual void onHold(TouchPoint p, unsigned long heldMs) {}
  virtual void onSwipe(SwipeDirection dir, TouchPoint start, TouchPoint end) {}
};
