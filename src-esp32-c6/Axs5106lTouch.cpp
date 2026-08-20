#include "Axs5106lTouch.h"

#include <Logger.hpp>

bool Axs5106lTouch::readRegs(uint8_t reg, uint8_t* buf, size_t len) {
  Wire.beginTransmission(_addr);
  Wire.write(reg);
  // STOP, а не repeated start - точно як в офіційному touch_i2c_read()
  // (ProcessExamples/ESP32_C6_Touch_LCD_1_47_LVGL_Animated_Clock/
  // esp_lcd_touch_axs5106l.cpp). З endTransmission(false) новий i2c-ng
  // драйвер валить наступний requestFrom() у ESP_ERR_INVALID_STATE (259).
  if (Wire.endTransmission() != 0) return false;

  if (Wire.requestFrom((int)_addr, (int)len) != (int)len) return false;
  for (size_t i = 0; i < len; ++i) buf[i] = Wire.read();
  return true;
}

bool Axs5106lTouch::probe(uint8_t addr) {
  // Ознака присутності - ACK на адресі, рівно те саме, що робить "i2cscan".
  //
  // Покладатися тут на ID-регістр НЕ можна: єдина перевірка, яку дає
  // оригінальний код, - "перший байт != 0", а чип має повне право віддати 0
  // (скажімо, поки не добіг внутрішній старт після reset). Через це робочий
  // тач оголошувався б відсутнім, і update() мовчки виходив би на
  // isPresent() == false - саме такий симптом і був: у логах порожньо.
  Wire.beginTransmission(addr);
  if (Wire.endTransmission() != 0) return false;

  _addr = addr;

  // ID читаємо вже суто для інформації - на рішення він не впливає.
  uint8_t id[3] = {};
  if (readRegs(REG_ID, id, sizeof(id))) {
    Logger::info("[AXS5106L] ID @0x%02X: %02X %02X %02X", addr, id[0], id[1], id[2]);
  }
  return true;
}

bool Axs5106lTouch::begin() {
  // Апаратне скидання. Тайминги (200 мс LOW, 300 мс на підйом) - з
  // оригінальної init-послідовності; коротші паузи давали нестабільний старт.
  if (_rst >= 0) {
    pinMode(_rst, OUTPUT);
    digitalWrite(_rst, LOW);
    delay(200);
    digitalWrite(_rst, HIGH);
    delay(300);
  }

  if (_int >= 0) {
    // INT не використовується (опитуємо поллінгом), але лишати його
    // плаваючим входом не варто - шумить.
    pinMode(_int, INPUT_PULLUP);
  }

  if (!probe(ADDR_PRIMARY) && !probe(ADDR_SECONDARY)) {
    _addr = 0;
    Logger::warn("[AXS5106L] не відгукнувся ні на 0x%02X, ні на 0x%02X (перевір: i2cscan)",
                 ADDR_PRIMARY, ADDR_SECONDARY);
    return false;
  }

  Logger::info("[AXS5106L] знайдено на 0x%02X", _addr);
  return true;
}

TouchPoint Axs5106lTouch::getPoint() { return _last; }

bool Axs5106lTouch::touched() {
  if (!isPresent()) return false;

  uint8_t data[14] = {};
  if (!readRegs(REG_TOUCH_DATA, data, sizeof(data))) return false;

  const uint8_t count = data[1];
  if (count == 0 || count > MAX_POINTS) return false;

  // Формат точки - 6 байт, починаючи з зміщення 2:
  //   [0] старші 4 біти X (молодші 4 біти байта) + прапорці події
  //   [1] молодші 8 біт X
  //   [2] старші 4 біти Y
  //   [3] молодші 8 біт Y
  //   [4],[5] - тиск/ID, тут не використовуються
  const uint8_t* p = data + 2;
  const uint16_t rawX = (uint16_t)((p[0] & 0x0F) << 8) | p[1];
  const uint16_t rawY = (uint16_t)((p[2] & 0x0F) << 8) | p[3];

  // Захист від сміття на шині: координати поза межами панелі означають, що
  // пакет прочитано неправильно - краще пропустити, ніж віддати хибний дотик.
  if (rawX >= _width || rawY >= _height) return false;

  _last = TouchPoint{(int)rawX, (int)rawY};
  return true;
}
