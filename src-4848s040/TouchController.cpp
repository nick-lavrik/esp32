#include <TouchController.h>
#include <TouchEvents.h>

#include <Logger.hpp>

#include "GT911Touch.h"

/*
#define TOUCH_SDA 19 - SDA (Serial Data) I²C
#define TOUCH_SCL 45 - SCL (Serial Clock) I²C
#define TOUCH_INT -1 // не підключений
#define TOUCH_RST -1 // не підключений

GT911Touch ts(TOUCH_SDA, TOUCH_SCL, TOUCH_INT, TOUCH_RST, 480, 480, 0x5D);
*/

// --- Тач ---
#define TOUCH_SDA 19
#define TOUCH_SCL 45
#define TOUCH_WIDTH TFT_WIDTH
#define TOUCH_HEIGHT TFT_HEIGHT
#define TOUCH_INT -1
#define TOUCH_RST -1

GT911Touch ts = GT911Touch(TOUCH_SDA, TOUCH_SCL, TOUCH_INT, TOUCH_RST, TOUCH_WIDTH, TOUCH_HEIGHT);

TouchEvents* TouchController::_events = nullptr;

void TouchController::setup(TouchEvents* events) {
  ts.begin();
  // #define ROTATION_LEFT      (uint8_t)0
  // #define ROTATION_INVERTED  (uint8_t)1
  // #define ROTATION_RIGHT     (uint8_t)2
  // #define ROTATION_NORMAL    (uint8_t)3
  ts.setRotation(TFT_ROTATION);  // ROTATION_NORMAL
  Logger::info("TouchController::GT911 setup done");
  _events = events;
}

void TouchController::update() { events().update(ts); }

TouchEvents& TouchController::events() { return *_events; }
