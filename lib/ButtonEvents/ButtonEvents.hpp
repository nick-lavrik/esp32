#pragma once

// Події однієї фізичної кнопки: press / release / click / double-click /
// long press. Лише логіка - пін читає той, хто кличе update() (src/Input/
// Buttons.cpp), тому клас не залежить від Arduino і тестується на хості
// (test/button_events/run.sh).
//
//   ButtonEvents button;
//   button.onEvent([](ButtonEvent e, uint32_t nowMs, uint32_t heldMs) { ... });
//   button.update(digitalRead(pin) == LOW, millis());   // щоітерації
//
// Порядок подій:
//   один клік        Press, Release, ...вікно doubleClickMs..., Click
//   подвійний клік   Press, Release, Press, Release, DoubleClick
//   утримання        Press, LongPress (на порозі, не чекаючи відпускання), Release
//
// Press/Release - миттєві й безумовні (стрибок у грі не чекає вікна
// double-click). Click навпаки спрацьовує із затримкою doubleClickMs: доки
// вікно не минуло, невідомо, чи буде другий клік. Утримання, що дійшло до
// LongPress, кліком не рахується.
//
// Брязкіт контактів: фронт приймається одразу, а наступна зміна стану -
// не раніше ніж через debounceMs. Тобто затримки на першому фронті немає,
// а дрижання після нього ігнорується (інакше відпускання давало б хибний
// double-click).

#include <stdint.h>

#include <functional>

enum class ButtonEvent : uint8_t { Press, Release, Click, DoubleClick, LongPress };

const char* buttonEventName(ButtonEvent event);

struct ButtonTiming {
  uint16_t debounceMs = 30;
  uint16_t doubleClickMs = 300;
  uint16_t longPressMs = 3000;
};

class ButtonEvents {
public:
  // heldMs - скільки кнопка була натиснута: для Release і LongPress, для
  // решти 0.
  using Callback = std::function<void(ButtonEvent event, uint32_t nowMs, uint32_t heldMs)>;

  explicit ButtonEvents(const ButtonTiming& timing = ButtonTiming()) : _timing(timing) {}

  void onEvent(Callback cb) { _cb = cb; }

  // down - кнопка натиснута зараз (рівень уже приведений до активного).
  void update(bool down, uint32_t nowMs);

  bool pressed() const { return _pressed; }

private:
  void emit(ButtonEvent event, uint32_t nowMs, uint32_t heldMs);

  ButtonTiming _timing;
  Callback _cb;
  bool _pressed = false;
  bool _longFired = false;
  uint8_t _clicks = 0;  // відпускань у поточному вікні double-click
  uint32_t _changeTs = 0;
  uint32_t _pressTs = 0;
  uint32_t _releaseTs = 0;
};
