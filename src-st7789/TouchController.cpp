#if __has_include(<XPT2046_Touchscreen.h>)
#include <TouchController.h>
#include <XPT2046_Touchscreen.h>

#include <Logger.hpp>

// --- Тач ---
#define TOUCH_CS 33
#define TOUCH_IRQ 36

// == I2C (SDA и SCL)
// SDA (Serial Data Line): линия, по которой передаются сами данные (и туда, и обратно).
// SCL (Serial Clock Line): линия тактового сигнала, которая задает общую скорость и ритм передачи.
//
// == SPI (Serial Peripheral Interface):
// SCK  / SCLK: тактовый сигнал.
// MOSI / SDI:  передача от главного к ведомому.
// MISO / SDO:  передача от ведомого к главному.
// CS   / SS:   выбор конкретного устройства для работы.

SPIClass touchSPI(HSPI);
// XPT2046_Touchscreen(uint8_t cspin, uint8_t tirq=255)
XPT2046_Touchscreen ts(TOUCH_CS, TOUCH_IRQ);

TouchEvents* TouchController::_events = nullptr;

void TouchController::setup(TouchEvents* events) {
  // bool begin(int8_t sck = -1, int8_t miso = -1, int8_t mosi = -1, int8_t ss = -1);
  touchSPI.begin(25, 39, 32, TOUCH_CS);
  ts.begin(touchSPI);
  ts.setRotation(TFT_ROTATION);  // 3

  Logger::info("TouchController::XPT2046 setup done");

  _events = events;
}

void TouchController::update() { events().update(ts); }

TouchEvents& TouchController::events() { return *_events; }
#endif