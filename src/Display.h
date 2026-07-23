// Display.h
#pragma once

#include <stdarg.h> // Обов'язково для роботи з трикрапкою (...)
#include "TftInstance.h"
// Для env:esp32-st7789     -> це справжній bodmer/TFT_eSPI (SPI, ST7789)
// Для env:esp32-4848s040   -> TFT_eSPI тут є alias'ом на LGFX (LovyanGFX,
//                             RGB-панель ST7701), визначеним у Setup_ST7701_4848S040.h
// Вибір відбувається через -DBOARD_4848S040 у build_flags конкретного env
// (TftInstance.h), прикладний код нижче однаковий для обох плат.

class Display {
public:
    Display();

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

    void pushImage(int32_t x, int32_t y, int32_t w, int32_t h, const uint16_t *data);
    void setCursor(int32_t x, int32_t y);

    // Ширина/висота активної області екрану (з урахуванням rotation)
    int width() const;
    int height() const;

    size_t   fontHeight() { return sprite_.fontHeight(); }
    void     setTextColor(uint16_t color) { sprite_.setTextColor(color); }
    uint16_t drawString(const char *text, int32_t x, int32_t y) { return sprite_.drawString(text, x, y); }

    int16_t textWidth(const char *string) { return sprite_.textWidth(string); }

    size_t printf(const __FlashStringHelper *ifsh, ...) {
        // 1. Створюємо список аргументів
        va_list args;
        
        // 2. Ініціалізуємо список (ifsh — останній відомий аргумент)
        va_start(args, ifsh);
        
        // 3. Приводимо тип через reinterpret_cast або (const char*)
        // У TFT_eSPI (і загалом в ESP32) vprintf очікує звичайний const char*
        const char *fmt = reinterpret_cast<const char *>(ifsh);
    
        // 4. Передаємо в метод vprintf вашого спрайту
        size_t result = sprite_.vprintf(fmt, args);
        
        // 4. Очищуємо список аргументів
        va_end(args);
        
        return result;
    }
    void setTextSize(uint8_t size) { sprite_.setTextSize(size); }

    size_t printf(const char *format, ...) {
        va_list args;
        va_start(args, format);
        // Передаємо напряму, оскільки тип вже const char*
        size_t result = sprite_.vprintf(format, args);
        va_end(args);
        return result;
    }

private:
    TFT_eSPI&   tft_;
    TFT_eSprite sprite_; // вся робота з екраном (drawText/clear/...) йде через спрайт,
                         // на реальний дисплей кадр потрапляє лише через flush()
 
    int width_  = 0;
    int height_ = 0;
};
