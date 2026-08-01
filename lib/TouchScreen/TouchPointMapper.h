#pragma once
#include "TouchScreenConfig.h"
#include "TouchPoint.h"

// Мапер: перетворює сирі координати дотику на реальні координати екрана
// з урахуванням калібрування/повороту/інверсії.
//
// Передається в TouchEvents::setTouchPointMapper(). Якщо не заданий -
// TouchEvents використовує сирі координати "як є" (без перетворення).
class TouchPointMapper {
public:
    explicit TouchPointMapper(const TouchScreenConfig &config = TouchScreenConfig());

    // void setConfig(const TouchScreenConfig &config);
    const TouchScreenConfig &getConfig() const { return _config; }

    // Основний метод перетворення. Викликається бібліотекою TouchEvents
    // автоматично щоразу, коли зафіксовано дотик.
    TouchPoint map(int x, int y) const;
    TouchPoint map(TouchPoint raw) const;

private:
    const TouchScreenConfig &_config;
};
