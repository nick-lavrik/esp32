#pragma once

#include <Arduino.h>
#include <Wire.h>

// Мінімальний драйвер QMI8658A (QST) - лише те, що потрібно для визначення
// орієнтації плати: WHO_AM_I, увімкнення акселерометра і читання XYZ.
// Гіроскоп навмисно НЕ вмикається (він тут не потрібен і лише їв би струм).
//
// Даташит: QMI8658A, 6-осьовий IMU, I2C до 400 кГц.
// Адреса залежить від рівня на SA0: 0x6B (SA0=1) або 0x6A (SA0=0).
// На ESP32-C6-Touch-LCD-1.47 це 0x6B - підтверджено на залізі ("i2cscan");
// fallback на 0x6A лишено для інших плат.
class Qmi8658 {
 public:
  // --- Регістри (підмножина) ---
  static constexpr uint8_t REG_WHO_AM_I = 0x00;  // має повернути 0x05
  static constexpr uint8_t REG_REVISION = 0x01;
  static constexpr uint8_t REG_CTRL1 = 0x02;  // інтерфейс: адресний автоінкремент
  static constexpr uint8_t REG_CTRL2 = 0x03;  // акселерометр: діапазон + ODR
  static constexpr uint8_t REG_CTRL3 = 0x04;  // гіроскоп: діапазон + ODR
  static constexpr uint8_t REG_CTRL7 = 0x08;  // вмикання: aEN (bit0), gEN (bit1)
  static constexpr uint8_t REG_AX_L = 0x35;   // далі поспіль AX_H, AY_L/H, AZ_L/H

  static constexpr uint8_t WHO_AM_I_VALUE = 0x05;

  static constexpr uint8_t ADDR_PRIMARY = 0x6B;    // SA0 = 1
  static constexpr uint8_t ADDR_SECONDARY = 0x6A;  // SA0 = 0

  // Знаходить чип на шині (пробує обидві адреси), налаштовує акселерометр на
  // ±4g / 250 Гц і вмикає його. Повертає false, якщо WHO_AM_I не збігся.
  bool begin(TwoWire& wire = Wire);

  // Читає прискорення у g. Повертає false при помилці I2C.
  bool readAccel(float& x, float& y, float& z);

  uint8_t address() const { return _addr; }
  bool isPresent() const { return _addr != 0; }

 private:
  bool readRegs(uint8_t reg, uint8_t* buf, size_t len);
  bool writeReg(uint8_t reg, uint8_t value);
  bool probe(uint8_t addr);

  TwoWire* _wire = nullptr;
  uint8_t _addr = 0;

  // Коефіцієнт переводу LSB -> g. МАЄ відповідати aFS у CTRL2 (див. begin()):
  // для ±4g на 16 бітах це 32768 / 4 = 8192 LSB/g. Якщо міняти діапазон -
  // міняти й це число, інакше показання поїдуть у рази.
  static constexpr float ACCEL_LSB_PER_G = 8192.0f;
};
