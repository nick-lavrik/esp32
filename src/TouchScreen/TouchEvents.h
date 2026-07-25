#pragma once
#include <Arduino.h>

#include "TouchScreenConfig.h"
#include "CallbackList.h"
#include "TouchPointMapper.h"
#include "TouchPoint.h"
#include "TouchCallback.h"
#include "HoldCallback.h"
#include "SwipeCallback.h"

// Очікується, що десь вже існує:
//   unsigned long millis();
//   bool ts.touched();
//   TS_Point ts.getPoint();  // з полями .x і .y

class TouchEvents {
public:
    char dummy[32] = "initial";

    // TouchCallback / HoldCallback / SwipeCallback - std::function (як TaskCallback у TaskController): підписатись можна і звичайною функцією,
    // і лямбдою із захопленням, напр.:
    //   int r = 5;
    //   touch.onTouch([r](TouchPoint p) { display.drawCircle(p.x, p.y, r, TFT_YELLOW); });

    explicit TouchEvents(const TouchScreenConfig &config = TouchScreenConfig());

    // ---- Підписка (кожна повертає handle для подальшої відписки) ----
    int onTouch(TouchCallback cb);
    int onHold(HoldCallback cb);
    int onDblClick(TouchCallback cb);
    int onSwipeLeft(SwipeCallback cb);
    int onSwipeRight(SwipeCallback cb);
    int onSwipeUp(SwipeCallback cb);
    int onSwipeDown(SwipeCallback cb);
    int onSwipeFromBottom(SwipeCallback cb);
    int onSwipeFromTop(SwipeCallback cb);
    int onSwipeFromLeft(SwipeCallback cb);
    int onSwipeFromRight(SwipeCallback cb);

    // ---- Відписка за handle, який повернув відповідний onXxx() ----
    void offTouch(int handle);
    void offHold(int handle);
    void offDblClick(int handle);
    void offSwipeLeft(int handle);
    void offSwipeRight(int handle);
    void offSwipeUp(int handle);
    void offSwipeDown(int handle);
    void offSwipeFromBottom(int handle);
    void offSwipeFromTop(int handle);
    void offSwipeFromLeft(int handle);
    void offSwipeFromRight(int handle);

    // Мапер сирих координат дотику в координати екрана.
    // nullptr (за замовчуванням) = координати передаються без змін.
    void setTouchPointMapper(TouchPointMapper *mapper) { _mapper = mapper; }

    // Головний метод — викликати щоразу в loop().
    void update(bool touched, TouchPoint point);

    // Зручна перевантажена версія - передайте сам об'єкт ts.
    // Лишається в заголовку, бо це шаблонний метод (тип TS невідомий заздалегідь).
    template <typename TS>
    void update(TS &ts) {
        if (ts.touched()) {
            auto p = ts.getPoint();
            TouchPoint raw{p.x, p.y};
            TouchPoint point = _mapper ? _mapper->map(raw) : raw;
            Serial.printf("touch raw{x: %d y:%d} screen{x: %d y: %d}\n", raw.x, raw.y, point.x, point.y);

            update(true, point);
        } else {
            update(false, TouchPoint{});
        }
    }

private:
    enum State { IDLE, PRESSED, HOLDING };

    void fireSwipe(int dx, int dy);

    TouchScreenConfig _config;
    TouchPointMapper *_mapper = nullptr;

    // Стан жесту
    State _state = IDLE;
    TouchPoint _start;
    TouchPoint _last;
    unsigned long _startTime = 0;
    bool _holdFired = false;
    unsigned long _lastTapTime = 0;

    // Списки колбеків - динамічні (std::vector), без обмеження кількості
    // підписників на один івент.
    CallbackList<TouchCallback> _onTouch;
    CallbackList<HoldCallback>  _onHold;
    CallbackList<TouchCallback> _onDblClick;
    CallbackList<SwipeCallback> _onSwipeLeft;
    CallbackList<SwipeCallback> _onSwipeRight;
    CallbackList<SwipeCallback> _onSwipeUp;
    CallbackList<SwipeCallback> _onSwipeDown;
    CallbackList<SwipeCallback> _onSwipeFromBottom;
    CallbackList<SwipeCallback> _onSwipeFromTop;
    CallbackList<SwipeCallback> _onSwipeFromLeft;
    CallbackList<SwipeCallback> _onSwipeFromRight;
};
