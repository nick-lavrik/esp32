#pragma once
#include <Arduino.h>

#include "TouchEventsConfig.h"
#include "CallbackList.h"
#include "TouchPointMapper.h"
#include "TouchPoint.h"

// Очікується, що десь вже існує:
//   unsigned long millis();
//   bool ts.touched();
//   TS_Point ts.getPoint();  // з полями .x і .y

class TouchEvents {
public:
    // Максимум одночасних колбеків на один івент. За потреби - збільш тут.
    static const int MAX_CALLBACKS = 4;

    typedef void (*TouchCallback)(TouchPoint point);
    typedef void (*HoldCallback)(TouchPoint point, unsigned long holdDurationMs);
    typedef void (*SwipeCallback)(TouchPoint start, TouchPoint end);

    explicit TouchEvents(const TouchEventsConfig &config = TouchEventsConfig());

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
            update(true, point);
        } else {
            update(false, TouchPoint{});
        }
    }

private:
    enum State { IDLE, PRESSED, HOLDING };

    void fireSwipe(int dx, int dy);

    TouchEventsConfig _config;
    TouchPointMapper *_mapper = nullptr;

    // Стан жесту
    State _state = IDLE;
    TouchPoint _start;
    TouchPoint _last;
    unsigned long _startTime = 0;
    bool _holdFired = false;
    unsigned long _lastTapTime = 0;

    // Списки колбеків (по декілька на кожен івент)
    CallbackList<TouchCallback, MAX_CALLBACKS> _onTouch;
    CallbackList<HoldCallback,  MAX_CALLBACKS> _onHold;
    CallbackList<TouchCallback, MAX_CALLBACKS> _onDblClick;
    CallbackList<SwipeCallback, MAX_CALLBACKS> _onSwipeLeft;
    CallbackList<SwipeCallback, MAX_CALLBACKS> _onSwipeRight;
    CallbackList<SwipeCallback, MAX_CALLBACKS> _onSwipeUp;
    CallbackList<SwipeCallback, MAX_CALLBACKS> _onSwipeDown;
    CallbackList<SwipeCallback, MAX_CALLBACKS> _onSwipeFromBottom;
    CallbackList<SwipeCallback, MAX_CALLBACKS> _onSwipeFromTop;
    CallbackList<SwipeCallback, MAX_CALLBACKS> _onSwipeFromLeft;
    CallbackList<SwipeCallback, MAX_CALLBACKS> _onSwipeFromRight;
};
