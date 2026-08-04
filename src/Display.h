// Display.h
#pragma once

#include <stdarg.h> // Обов'язково для роботи з трикрапкою (...)
#include <atomic>
#include <TLogger.hpp>
#include "TftInstance.h"
// Для env:esp32-st7789     -> це справжній bodmer/TFT_eSPI (SPI, ST7789)
// Для env:esp32-4848s040   -> TFT_eSPI тут є alias'ом на LGFX (LovyanGFX,
//                             RGB-панель ST7701), визначеним у Setup_ST7701_4848S040.h
// Вибір відбувається через -DBOARD_4848S040 у build_flags конкретного env
// (TftInstance.h), прикладний код нижче однаковий для обох плат.
class Display {
public:

    void startWrite() { tft_.startWrite(); }
    void endWrite() { tft_.endWrite(); }

    explicit Display();
    void flip();
    uint8_t getRotation() const { return tft_.getRotation(); }
    void setRotation(uint8_t r) { tft_.setRotation(r);       }

    // Ініціалізація дисплея (обов'язково викликати в setup())
    void init();

    // Заливка всього екрану кольором
    void clear(uint16_t color = TFT_BLACK);

    // Малювання тексту в позиції (x, y)
    void drawText(int x, int y, const char* text, uint16_t color);

    // Малювання тексту по центру екрана
    void drawCenteredText(const char* text, uint16_t color, uint8_t fontSize = 4);

    // Виводить накопичений у спрайті кадр на реальний екран.
    // Викликати після того, як усе малювання кадру завершено.
    void flush();

    uint8_t brightness() { return brightness_; } // percent!
    void brightness(uint8_t percent);

    void pushImage(int32_t x, int32_t y, int32_t w, int32_t h, const uint16_t *data);
    void setCursor(int32_t x, int32_t y);

    // Ширина/висота активної області екрану (з урахуванням rotation)
    int width() const;
    int height() const;

    const uint32_t loopFrameRate();
    size_t   fontHeight() { return sprite_.fontHeight(); }

    void     setTextFont(uint8_t f)                    { sprite_.setTextFont(f); }
    void     setTextColor(uint16_t color)              { sprite_.setTextColor(color); }
    void     setTextColor(uint16_t color, uint16_t bg) { sprite_.setTextColor(color, bg); }
    void     setTextSize(uint8_t size)                 { sprite_.setTextSize(size); }

    void     drawRect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color) { sprite_.drawRect(x, y, w, h, color); }
    void     drawCircle(int32_t x, int32_t y, int32_t r, uint32_t color)          { sprite_.drawCircle(x, y, r, color); }
    uint16_t drawString(const char *text, int32_t x, int32_t y)                   { return sprite_.drawString(text, x, y); }

    int16_t  textWidth(const char *string) { return sprite_.textWidth(string); }

    size_t print(const char *string)   { return sprite_.print(string); }
    size_t println(const char *string) { return sprite_.println(string); }
    /* void getTextBounds(const char *str, int16_t x, int16_t y,
                    int16_t *x1, int16_t *y1, uint16_t *w, uint16_t *h)
    {
        sprite_.getTextBounds(str, x, y, x1, y1, w, h);
    } */

    template <typename... Args>
    size_t printf(const __FlashStringHelper *ifsh, const Args&... args) {
        // return sprite_.printf(reinterpret_cast<const char*>(ifsh), args...); 
        return sprite_.printf((PGM_P)ifsh, args...); 
    }

    template <typename... Args>
    size_t printf(const char* format, const Args&... args) {
        return sprite_.printf(format, args...); 
    }

protected:

private:
    TFT_eSPI&   tft_;
    TFT_eSprite sprite_; // вся робота з екраном (drawText/clear/...) йде через спрайт,
                         // на реальний дисплей кадр потрапляє лише через flush()
 
    int width_  = 0;
    int height_ = 0;
    uint8_t brightness_ = 50; // percent!

    TLogger _logger{"tft"};
};
