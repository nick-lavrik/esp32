#include "Qmi8658.h"

#include <Logger.hpp>

bool Qmi8658::readRegs(uint8_t reg, uint8_t* buf, size_t len) {
  _wire->beginTransmission(_addr);
  _wire->write(reg);
  // endTransmission() = STOP, а НЕ endTransmission(false).
  //
  // Repeated start тут ламає новий i2c-ng драйвер arduino-esp32 3.x:
  // наступний requestFrom() падає з ESP_ERR_INVALID_STATE (259) і засипає
  // консоль. Офіційний драйвер Waveshare (див.
  // ProcessExamples/ESP32_C6_Touch_LCD_1_47_LVGL_Animated_Clock/
  // esp_lcd_touch_axs5106l.cpp, touch_i2c_read) робить саме STOP - обидва
  // чипи на цій шині це коректно тримають.
  if (_wire->endTransmission() != 0) return false;

  if (_wire->requestFrom((int)_addr, (int)len) != (int)len) return false;
  for (size_t i = 0; i < len; ++i) buf[i] = _wire->read();
  return true;
}

bool Qmi8658::writeReg(uint8_t reg, uint8_t value) {
  _wire->beginTransmission(_addr);
  _wire->write(reg);
  _wire->write(value);
  return _wire->endTransmission() == 0;
}

bool Qmi8658::probe(uint8_t addr) {
  _addr = addr;
  uint8_t who = 0;
  if (!readRegs(REG_WHO_AM_I, &who, 1)) return false;
  return who == WHO_AM_I_VALUE;
}

bool Qmi8658::begin(TwoWire& wire) {
  _wire = &wire;

  if (!probe(ADDR_PRIMARY) && !probe(ADDR_SECONDARY)) {
    _addr = 0;
    return false;
  }

  uint8_t rev = 0;
  readRegs(REG_REVISION, &rev, 1);
  Logger::info("[QMI8658] знайдено на 0x%02X (revision 0x%02X)", _addr, rev);

  // CTRL1 = 0x40: ADDR_AI (bit6) - автоінкремент адреси при блоковому читанні.
  // Без нього шість байтів XYZ довелось би читати по одному регістру.
  if (!writeReg(REG_CTRL1, 0x40)) return false;

  // CTRL2 = 0x13: aFS = 0b001 (біти 6:4), aODR = 0b0011 (біти 3:0).
  //
  // УВАГА на таблицю aFS: 000=±2g, 001=±4g, 010=±8g, 011=±16g. Тут раніше
  // стояло 0x23 (aFS=0b010) з коментарем "±4g" - насправді це ±8g, тобто
  // масштаб був удвічі грубіший за той, що використовувався в перерахунку,
  // і ВСІ значення виходили рівно вдвічі меншими. Через це лежача плата
  // давала ~0.46g замість ~0.92g, поріг 0.6g не досягався, і орієнтація
  // назавжди лишалась Unknown.
  if (!writeReg(REG_CTRL2, 0x13)) return false;

  // CTRL3 = 0x00: гіроскоп вимкнений (не потрібен для орієнтації).
  if (!writeReg(REG_CTRL3, 0x00)) return false;

  // CTRL7 = 0x01: aEN (bit0) - вмикаємо лише акселерометр, gEN лишається 0.
  if (!writeReg(REG_CTRL7, 0x01)) return false;

  delay(20);  // чипу потрібен час на перший вимір після ввімкнення
  return true;
}

bool Qmi8658::readAccel(float& x, float& y, float& z) {
  if (!isPresent()) return false;

  uint8_t raw[6] = {};
  if (!readRegs(REG_AX_L, raw, sizeof(raw))) return false;

  // Little-endian, знакові 16-бітні. Приведення через int16_t обов'язкове:
  // без нього від'ємні значення (а саме вони й означають "перевернуто")
  // перетворились би на великі додатні.
  const int16_t rx = (int16_t)((uint16_t)raw[1] << 8 | raw[0]);
  const int16_t ry = (int16_t)((uint16_t)raw[3] << 8 | raw[2]);
  const int16_t rz = (int16_t)((uint16_t)raw[5] << 8 | raw[4]);

  x = rx / ACCEL_LSB_PER_G;
  y = ry / ACCEL_LSB_PER_G;
  z = rz / ACCEL_LSB_PER_G;
  return true;
}
