#include "ButtonEvents.hpp"

const char* buttonEventName(ButtonEvent event) {
  switch (event) {
    case ButtonEvent::Press:
      return "press";
    case ButtonEvent::Release:
      return "release";
    case ButtonEvent::Click:
      return "click";
    case ButtonEvent::DoubleClick:
      return "double-click";
    case ButtonEvent::LongPress:
      return "long-press";
  }
  return "?";
}

void ButtonEvents::emit(ButtonEvent event, uint32_t nowMs, uint32_t heldMs) {
  if (_cb) _cb(event, nowMs, heldMs);
}

void ButtonEvents::update(bool down, uint32_t nowMs) {
  const bool canChange = nowMs - _changeTs >= _timing.debounceMs;

  if (down && !_pressed && canChange) {
    _pressed = true;
    _longFired = false;
    _changeTs = nowMs;
    _pressTs = nowMs;
    emit(ButtonEvent::Press, nowMs, 0);
  } else if (!down && _pressed && canChange) {
    _pressed = false;
    _changeTs = nowMs;
    _releaseTs = nowMs;
    emit(ButtonEvent::Release, nowMs, nowMs - _pressTs);
    if (_longFired) return;
    if (++_clicks < 2) return;
    _clicks = 0;
    emit(ButtonEvent::DoubleClick, nowMs, 0);
  } else if (_pressed && !_longFired && nowMs - _pressTs >= _timing.longPressMs) {
    _longFired = true;
    _clicks = 0;
    emit(ButtonEvent::LongPress, nowMs, nowMs - _pressTs);
  } else if (!_pressed && _clicks == 1 && nowMs - _releaseTs > _timing.doubleClickMs) {
    _clicks = 0;
    emit(ButtonEvent::Click, nowMs, 0);
  }
}
