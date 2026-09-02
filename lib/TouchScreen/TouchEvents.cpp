#include "TouchEvents.h"

#include <math.h>

#include <utility>  // std::move

#include "TouchScreenConfig.h"

TouchEvents::TouchEvents(const TouchScreenConfig &config) : _config(config) {}

// ---- Підписка ----
// cb переміщується (std::move) далі в CallbackList::add - без зайвої копії
// замикання, так само як TaskScheduler::addCronTask/addJob роблять з TaskCallback.
int TouchEvents::onPress(TouchCallback cb) { return _onPress.add(std::move(cb)); }
int TouchEvents::onRelease(TouchCallback cb) { return _onRelease.add(std::move(cb)); }
int TouchEvents::onTouch(TouchCallback cb) { return _onTouch.add(std::move(cb)); }
int TouchEvents::onHold(HoldCallback cb) { return _onHold.add(std::move(cb)); }
int TouchEvents::onDblClick(TouchCallback cb) { return _onDblClick.add(std::move(cb)); }
int TouchEvents::onSwipeLeft(SwipeCallback cb) { return _onSwipeLeft.add(std::move(cb)); }
int TouchEvents::onSwipeRight(SwipeCallback cb) { return _onSwipeRight.add(std::move(cb)); }
int TouchEvents::onSwipeUp(SwipeCallback cb) { return _onSwipeUp.add(std::move(cb)); }
int TouchEvents::onSwipeDown(SwipeCallback cb) { return _onSwipeDown.add(std::move(cb)); }
int TouchEvents::onSwipeFromBottom(SwipeCallback cb) {
  return _onSwipeFromBottom.add(std::move(cb));
}
int TouchEvents::onSwipeFromTop(SwipeCallback cb) { return _onSwipeFromTop.add(std::move(cb)); }
int TouchEvents::onSwipeFromLeft(SwipeCallback cb) { return _onSwipeFromLeft.add(std::move(cb)); }
int TouchEvents::onSwipeFromRight(SwipeCallback cb) { return _onSwipeFromRight.add(std::move(cb)); }

// ---- Відписка ----
void TouchEvents::offPress(int handle) { _onPress.remove(handle); }
void TouchEvents::offRelease(int handle) { _onRelease.remove(handle); }
void TouchEvents::offTouch(int handle) { _onTouch.remove(handle); }
void TouchEvents::offHold(int handle) { _onHold.remove(handle); }
void TouchEvents::offDblClick(int handle) { _onDblClick.remove(handle); }
void TouchEvents::offSwipeLeft(int handle) { _onSwipeLeft.remove(handle); }
void TouchEvents::offSwipeRight(int handle) { _onSwipeRight.remove(handle); }
void TouchEvents::offSwipeUp(int handle) { _onSwipeUp.remove(handle); }
void TouchEvents::offSwipeDown(int handle) { _onSwipeDown.remove(handle); }
void TouchEvents::offSwipeFromBottom(int handle) { _onSwipeFromBottom.remove(handle); }
void TouchEvents::offSwipeFromTop(int handle) { _onSwipeFromTop.remove(handle); }
void TouchEvents::offSwipeFromLeft(int handle) { _onSwipeFromLeft.remove(handle); }
void TouchEvents::offSwipeFromRight(int handle) { _onSwipeFromRight.remove(handle); }

/* void drawTouchPoint(bool touched, TouchPoint p) {
  static uint32_t lastDump = millis();
  if (ts.touched()) {
    TS_Point p = ts.getPoint();
    static int minX = 4095, maxX = 0, minY = 4095, maxY = 0;
    if (p.x < minX) minX = p.x;
    if (p.x > maxX) maxX = p.x;
    if (p.y < minY) minY = p.y;
    if (p.y > maxY) maxY = p.y;
    // Serial.printf("raw(x=%4d y=%4d) | screen(x=%4d y=%4d) | bounds: X[%4d-%4d] Y[%4d-%4d]\n",
p.x, p.y, mapTouchX(p.x), mapTouchY(p.y), minX, maxX, minY, maxY); TouchPoint p2 = mapper.map(p.x,
p.y);

    img.drawCircle(p2.x, p2.y, 3, TFT_RED);
    img.fillCircle(p2.x, p2.y, 2, TFT_YELLOW);
    Serial.printf("raw(x=%4d y=%4d) | screen(x=%4d y=%4d) | map(x=%4d y=%4d) | bounds: X[%4d-%4d]
Y[%4d-%4d]\n", p.x, p.y, mapTouchX(p.x), mapTouchY(p.y), p2.x, p2.y, minX, maxX, minY, maxY);
    lastDump = millis();
  }
} */

void TouchEvents::update(bool touched, TouchPoint point) {
  unsigned long now = millis();

  if (touched) {
    if (_state == IDLE) {
      // Початок нового дотику
      _start = point;
      _startTime = now;
      _last = point;
      _state = PRESSED;
      _holdFired = false;
      _onPress.invoke(point);
    } else {
      _last = point;

      if (!_holdFired && (now - _startTime) >= _config.holdThresholdMs) {
        _onHold.invoke(point, now - _startTime);
        _holdFired = true;
        _state = HOLDING;
      }
    }
    return;
  }

  // Палець відпущено
  if (_state == PRESSED || _state == HOLDING) {
    // ДО розбору свайп/тап: onRelease парний до onPress і мусить прийти
    // незалежно від того, чим виявився жест.
    _onRelease.invoke(_last);

    unsigned long duration = now - _startTime;
    int dx = _last.x - _start.x;
    int dy = _last.y - _start.y;
    unsigned int distance = (int)sqrt((double)(dx * dx + dy * dy));

    if (!_holdFired) {
      if (distance >= _config.swipeMinDistancePx && duration <= _config.swipeMaxDurationMs) {
        fireSwipe(dx, dy);
      } else {
        // Це тап
        _onTouch.invoke(_start);

        if ((now - _lastTapTime) <= _config.dblClickIntervalMs) {
          _onDblClick.invoke(_start);
          _lastTapTime = 0;  // щоб третій тап не «доклацнув» подвійний
        } else {
          _lastTapTime = now;
        }
      }
    }
    _state = IDLE;
  }
}

void TouchEvents::fireSwipe(int dx, int dy) {
  bool horizontal = abs(dx) > abs(dy);

  if (horizontal) {
    if (dx > 0) {
      // рух вправо: старт біля лівого краю -> "свайп від лівого краю"
      if (abs(_start.x) <= _config.edgeZoneX) {
        _onSwipeFromLeft.invoke(_start, _last);
      } else {
        _onSwipeRight.invoke(_start, _last);
      }
    } else {
      // рух вліво: старт біля правого краю -> "свайп від правого краю"
      if (abs(_start.x) >= _config.screenWidth - _config.edgeZoneX) {
        _onSwipeFromRight.invoke(_start, _last);
      } else {
        _onSwipeLeft.invoke(_start, _last);
      }
    }
  } else {
    if (dy < 0) {
      // рух вгору: старт біля низу -> "свайп від низу"
      if (abs(_start.y) >= _config.screenHeight - _config.edgeZoneY) {
        _onSwipeFromBottom.invoke(_start, _last);
      } else {
        _onSwipeUp.invoke(_start, _last);
      }
    } else {
      // рух вниз: старт біля верху -> "свайп від верху"
      if (abs(_start.y) <= _config.edgeZoneY) {
        _onSwipeFromTop.invoke(_start, _last);
      } else {
        _onSwipeDown.invoke(_start, _last);
      }
    }
  }
}
