// Спільна шина I2C (тач + IMU на esp32-c6): піднімається один раз, скан.

#include <Arduino.h>

#include <Logger.hpp>

#include "App/AppGlobals.hpp"
#include "Input.hpp"

#if defined(I2C_SDA) && defined(I2C_SCL)
// <Wire.h> через макрос, а не буквально. LDF (режим chain) не обчислює #if:
// буквальний include тут додав би бібліотеку Wire на ВСІ env, а її глобальний
// TwoWire з конструктором лінкер не викидає - +35 КБ флешу на esp32-c3/s3, де
// I2C немає. Макрос LDF не розгортає; на платах з I2C Wire і так приходить
// через їхній драйвер (src-esp32-c6/Axs5106lTouch.h, TAMC_GT911 на 4848s040).
#define I2C_BUS_WIRE_HEADER <Wire.h>
#include I2C_BUS_WIRE_HEADER
#endif

// I2C-шина СПІЛЬНА для тача й IMU, тому Wire.begin() робиться рівно один раз
// тут, а не в кожному драйвері: повторний Wire.begin() з тими самими пінами
// нешкідливий, але з РІЗНИМИ - мовчки переприв'язує шину і ламає той
// пристрій, що ініціалізувався першим.
void setupI2C() {
#if defined(I2C_SDA) && defined(I2C_SCL)
  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(400000);
  Logger::info("I2C: SDA=%d SCL=%d @400kHz", I2C_SDA, I2C_SCL);
#endif
}

// Скан шини. Потрібен, бо документація і сторонні драйвери розходяться в
// адресах (AXS5106L: 0x51 у Waveshare FAQ проти 0x63 у toto04/axs5106l),
// а єдиний спосіб дізнатися правду - спитати саму плату.
void i2cScan() {
#if defined(I2C_SDA) && defined(I2C_SCL)
  Logger::info("========= I2C scan (SDA=%d SCL=%d) =========================", I2C_SDA, I2C_SCL);

  int found = 0;
  for (uint8_t addr = 0x08; addr < 0x78; ++addr) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      ++found;
      const char* known = "";
      if (addr == 0x6B || addr == 0x6A)
        known = " <- QMI8658A (IMU)";
      else if (addr == 0x63 || addr == 0x51)
        known = " <- AXS5106L (touch)";
      Logger::info("  0x%02X%s", addr, known);
    }
  }

  Logger::info("------------------------------------------------------------");
  Logger::info(found ? "Devices found: %d" : "Nothing found - check pins/power", found);
  Logger::info("============================================================");
#else
  Logger::error("I2C diabled (I2C_SDA / I2C_SCL not defined)");
#endif
}

void registerI2cCommands(SerialCommander& commander) {
#if defined(I2C_SDA) && defined(I2C_SCL)
  commander.registerCommand("i2cscan", "scan I2C bus and list device addresses", [](const String& args) { i2cScan(); });
#endif
  (void)commander;
}
