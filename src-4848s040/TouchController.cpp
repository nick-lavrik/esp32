// #include <TAMC_GT911.h>
#include "GT911Touch.h"
#include "TouchScreen/TouchEvents.h"
#include "TouchScreen/TouchController.h"
/*
#define TOUCH_SDA 19
#define TOUCH_SCL 45
#define TOUCH_INT -1   // не підключений
#define TOUCH_RST -1   // не підключений

GT911Touch ts(TOUCH_SDA, TOUCH_SCL, TOUCH_INT, TOUCH_RST, 480, 480, 0x5D);
*/

// --- Тач ---
#define TOUCH_SDA 19
#define TOUCH_SCL 45
#define TOUCH_WIDTH  480
#define TOUCH_HEIGHT 480
#define TOUCH_INT -1
#define TOUCH_RST -1

GT911Touch ts = GT911Touch(TOUCH_SDA, TOUCH_SCL, TOUCH_INT, TOUCH_RST, TOUCH_WIDTH, TOUCH_HEIGHT);

TouchEvents* TouchController::_events = nullptr;

void TouchController::setup(TouchEvents* events) {

  ts.begin();
  ts.setRotation(ROTATION_NORMAL);
  // ts.setRotation(ROTATION_INVERTED);

  _events = events;
}

void TouchController::update() {
    events().update(ts);
}

TouchEvents& TouchController::events() {
    return *_events;
}
