#include <TouchController.h>
#include <TouchEvents.h>

#include <Logger.hpp>

#include "GT911Touch.h"

// Піни беруться з build_flags (I2C_SDA/I2C_SCL/TOUCH_INT/TOUCH_RST), а не
// хардкодяться тут: ті самі імена використовує esp32-c6, щоб однакова річ
// однаково називалась на всіх платах (див. "Єдині назви прапорців" у
// docs/architecture.md). INT/RST на цій платі не розведені, тому -1.
//
// Wire.begin() робить main.cpp::setupI2C() ДО setupTouchScreen(). TAMC_GT911
// усередині begin() викликає свій Wire.begin() з тими самими пінами -
// повторний виклик з ІДЕНТИЧНИМИ пінами нешкідливий (з іншими - мовчки
// переприв'язав би шину).
GT911Touch ts = GT911Touch(I2C_SDA, I2C_SCL, TOUCH_INT, TOUCH_RST, TFT_WIDTH, TFT_HEIGHT);

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
