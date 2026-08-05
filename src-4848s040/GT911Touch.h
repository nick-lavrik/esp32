#pragma once
#if __has_include(<TAMC_GT911.h>)
#include <Arduino.h>
#include <TAMC_GT911.h>  // lib_deps: tamctec/TAMC_GT911@^1.0.2
#include <TouchPoint.h>

// Обгортка над TAMC_GT911, яка надає той самий інтерфейс, що й
// XPT2046_Touchscreen (touched() + getPoint()).
//
// Завдяки цьому TouchEvents::update(TS &ts) - шаблонний метод з
// TouchEvents.h - працює однаково і з XPT2046_Touchscreen (SPI, ST7789),
// і з GT911Touch (I2C, 4848S040), без жодних змін в TouchEvents/CallbackList/
// TouchPointMapper/TouchScreenConfig/TouchEventsConfig.
class GT911Touch {
public:
  // sda/scl       - піни I2C
  // interruptPin  - INT пін GT911 (можна -1, якщо не підключений)
  // resetPin      - RST пін GT911 (можна -1, якщо не підключений)
  // width/height  - роздільна здатність панелі (480x480 для 4848S040)
  // i2cAddr       - адреса на шині I2C (типово 0x5D, іноді 0x14)
  GT911Touch(uint8_t sda, uint8_t scl, int8_t interruptPin, int8_t resetPin, uint16_t width,
             uint16_t height, uint8_t i2cAddr = GT911_ADDR1);

  void begin();
  void setRotation(uint8_t rotation);

  // ---- Інтерфейс, сумісний з XPT2046_Touchscreen ----
  bool touched();  // виконує I2C-опитування контролера
  TouchPoint getPoint();  // координати першого активного дотику (кеш touched())

private:
  TAMC_GT911 _gt911;
  uint8_t _i2cAddr;
};
#endif