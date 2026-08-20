#pragma once

#include <Arduino.h>
#include <Wire.h>

#include <TouchPoint.h>

// Драйвер ємнісного тача AXS5106L (I2C) для ESP32-C6-Touch-LCD-1.47.
//
// ГОТОВОЇ бібліотеки під Arduino для цього чипа немає. Протокол відновлено з
// github.com/toto04/axs5106l (порт офіційного C-прикладу Waveshare) - його
// автор сам попереджає, що код може містити помилки, тому все, що можна було
// перевірити незалежно, звірено з docs.waveshare.com/ESP32-C6-Touch-LCD-1.47.
//
// I2C-адреса: 0x63, ПІДТВЕРДЖЕНО на залізі ("i2cscan" бачить рівно її).
// Waveshare FAQ називає 0x51 - для цієї плати це неправда. begin() усе одно
// пробує обидві (0x63 першою), щоб не зламатися на іншій ревізії плати.
class Axs5106lTouch {
 public:
  static constexpr uint8_t REG_ID = 0x08;          // 3 байти, [0] != 0 = чип живий
  static constexpr uint8_t REG_TOUCH_DATA = 0x01;  // 14 байт даних дотику

  static constexpr uint8_t ADDR_PRIMARY = 0x63;
  static constexpr uint8_t ADDR_SECONDARY = 0x51;

  // Максимум точок, які реально розбираються. Пакет має місце під дві.
  static constexpr uint8_t MAX_POINTS = 2;

  Axs5106lTouch(int8_t sda, int8_t scl, int8_t intPin, int8_t rstPin, uint16_t width,
                uint16_t height)
      : _sda(sda), _scl(scl), _int(intPin), _rst(rstPin), _width(width), _height(height) {}

  // Скидання чипа і пошук на шині. Wire.begin() має бути вже виконаний
  // (шина спільна з IMU, тому піднімається один раз у main.cpp::setupI2C()).
  bool begin();

  // Контракт, якого чекає шаблонний TouchEvents::update(TS&) - той самий, що
  // в GT911Touch і XPT2046_Touchscreen: touched() робить I2C-опитування,
  // getPoint() віддає закешований результат. Саме ця пара (а не власний
  // read()) потрібна, щоб спрацював TouchPointMapper - у не-шаблонній
  // перевантаженій версії update(bool, TouchPoint) мапер не застосовується.
  bool touched();
  TouchPoint getPoint();

  bool isPresent() const { return _addr != 0; }
  uint8_t address() const { return _addr; }

 private:
  bool readRegs(uint8_t reg, uint8_t* buf, size_t len);
  bool probe(uint8_t addr);

  int8_t _sda, _scl, _int, _rst;
  uint16_t _width, _height;
  uint8_t _addr = 0;

  // Кеш останнього успішного читання - getPoint() викликається одразу після
  // touched() і не має робити другу I2C-транзакцію.
  TouchPoint _last{};
};
