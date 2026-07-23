// Display.h
#pragma once

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

    // Ширина/висота активної області екрану (з урахуванням rotation)
    int width() const;
    int height() const;

private:
    TFT_eSPI&   tft_;
    TFT_eSprite sprite_; // вся робота з екраном (drawText/clear/...) йде через спрайт,
                         // на реальний дисплей кадр потрапляє лише через flush()
 
    int width_  = 0;
    int height_ = 0;
};
