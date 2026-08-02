#pragma once

#include <Arduino.h>

// Обгортка над статичним монохромним зображенням у PROGMEM.
//
// Формат буфера - той самий, що очікує Adafruit_GFX::drawBitmap():
// 1 біт/піксель, MSB = лівий піксель рядка, кожен рядок доповнений
// до цілого байта: rowStrideBytes() == ceil(width/8).
//
// Масив даних генерується заздалегідь (див. tools/img_to_mono.py) і
// підключається як звичайний extern const uint8_t[] PROGMEM.
class MonoBitmap
{
public:
    constexpr MonoBitmap(const uint8_t *data, uint16_t width, uint16_t height)
        : _data(data), _width(width), _height(height)
    {
    }

    const uint8_t *data() const { return _data; }
    uint16_t width() const { return _width; }
    uint16_t height() const { return _height; }

    size_t rowStrideBytes() const { return ((size_t)_width + 7) / 8; }
    size_t sizeBytes() const { return rowStrideBytes() * _height; }

    // Малює зображення на будь-якому Adafruit_GFX-сумісному об'єкті
    // (в т.ч. на TFT_eSPI-обгортці над Adafruit_SSD1306 з Setup_SSD1306_NodeMCU.h)
    template <typename TDisplay>
    void draw(TDisplay &display, int16_t x, int16_t y, uint16_t color = 1) const
    {
        display.drawBitmap(x, y, _data, _width, _height, color);
    }

private:
    const uint8_t *_data;
    uint16_t _width;
    uint16_t _height;
};
