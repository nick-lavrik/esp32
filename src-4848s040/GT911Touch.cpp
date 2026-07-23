#include "GT911Touch.h"

GT911Touch::GT911Touch(uint8_t sda, uint8_t scl, uint8_t interruptPin, uint8_t resetPin,
                       uint16_t width, uint16_t height, uint8_t i2cAddr)
    : _gt911(sda, scl, interruptPin, resetPin, width, height),
      _i2cAddr(i2cAddr) 
{
   // _gt911 = TAMC_GT911(sda, scl, interruptPin, resetPin, width, height);
}

void GT911Touch::begin() {
   _gt911.begin(_i2cAddr);
   // _gt911.setRotation(ROTATION_NORMAL);
}

// Встановлення орієнтації екрана
void GT911Touch::setRotation(uint8_t rotation) {
    _gt911.setRotation(rotation);
}

// Опитування та перевірка наявності дотику (сумісність з XPT2046)
bool GT911Touch::touched() {
    _gt911.read(); // Зчитуємо дані з шини I2C
    return (_gt911.isTouched && _gt911.touches > 0);
}

// Отримання координат першого дотику (сумісність з XPT2046)
TouchPoint GT911Touch::getPoint() {
    // Якщо дотиків немає, повертаємо порожню/дефолтну точку
    if (_gt911.touches == 0) {
        return TouchPoint(0, 0); 
    }
    
    // Беремо координати першої точки (індекс 0)
    uint16_t x = _gt911.points[0].x;
    uint16_t y = _gt911.points[0].y;
    // uint16_t z = _gt911.points[0].size; // Використовуємо розмір плями як силу натискання (Z)

    return TouchPoint(x, y);
}
