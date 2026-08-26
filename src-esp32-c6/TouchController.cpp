#include <TouchController.h>
#include <TouchEvents.h>

#include <Logger.hpp>

#include "Axs5106lTouch.h"

// Реалізація TouchController для esp32-c6 (AXS5106L, I2C).
// Аналог src-4848s040/TouchController.cpp (GT911), див. TouchController.h.

// Панель нативно портретна 172x320; TFT_WIDTH/TFT_HEIGHT саме такі, а
// TFT_ROTATION=3 повертає її вже на рівні дисплея. Контролер тача віддає
// СИРІ координати в нативних осях панелі, тому тут вони й перевіряються, а
// перерахунок під поворот робить TouchPointMapper (див. TouchScreenConfig).
static Axs5106lTouch ts(I2C_SDA, I2C_SCL, TOUCH_INT, TOUCH_RST, TFT_WIDTH, TFT_HEIGHT);

TouchEvents* TouchController::_events = nullptr;

void TouchController::setup(TouchEvents* events) {
  if (!ts.begin()) {
    Logger::warn("TouchController::AXS5106L unavailable - touch disabled");
  } else {
    Logger::info("TouchController::AXS5106L setup done (0x%02X)", ts.address());
  }
  _events = events;
}

void TouchController::update() {
  if (!_events || !ts.isPresent()) return;

  // Обмеження частоти опитування. Офіційний драйвер читає тач лише по
  // перериванню TP_INT, а тут - поллінг, тому без цього обмеження на кожен
  // кадр loop() йшла б 14-байтна I2C-транзакція по шині, спільній з IMU.
  // 20 мс = 50 Гц: для пальця це непомітно, а шину розвантажує суттєво.
  static uint32_t lastPollMs = 0;
  const uint32_t now = millis();
  if (now - lastPollMs < 20) return;
  lastPollMs = now;

  events().update(ts);
}

TouchEvents& TouchController::events() { return *_events; }
