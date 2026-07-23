#include <XPT2046_Touchscreen.h>
#include "TouchController.h"


// --- Тач ---
#define TOUCH_CS   33
#define TOUCH_IRQ  36

SPIClass touchSPI(HSPI);
XPT2046_Touchscreen ts(TOUCH_CS, TOUCH_IRQ);

TouchEvents* TouchController::_events = nullptr;

void TouchController::setup(TouchEvents* events) {
  touchSPI.begin(25, 39, 32, TOUCH_CS);
  ts.begin(touchSPI);
  ts.setRotation(3);
  Serial.println("TouchScreen initialised()");

  _events = events;
}

void TouchController::update() {
    events().update(ts);
}

TouchEvents& TouchController::events() {
    return *_events;
}