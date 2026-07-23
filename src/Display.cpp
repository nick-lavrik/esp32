// Display.cpp
#include "Display.h"

// Глобальний об'єкт "tft" створюється рівно в одному файлі на середовище:
//   env:esp32-st7789      -> src/TftInstance_ST7789.cpp
//   env:esp32-4848s040    -> src/TftInstance_4848S040.cpp
// Тут ми лише беремо на нього посилання.

Display::Display() : tft_(tft), sprite_(&tft_) {}

void Display::init() {
    tft_.init();
    tft_.setRotation(TFT_ROTATION);

    width_  = tft_.width();
    height_ = tft_.height();

    // Спрайт на весь розмір екрана — вся подальша робота йде через нього.
    sprite_.setColorDepth(16);
    sprite_.createSprite(width_, height_);
    sprite_.fillSprite(TFT_BLACK);

    flush(); // одразу показуємо чорний кадр, щоб не лишався сміттєвий вміст VRAM
}

void Display::clear(uint16_t color) {
    sprite_.fillSprite(color);
}

void Display::drawText(int x, int y, const char* text, uint16_t color) {
    sprite_.setTextColor(color);
    sprite_.drawString(text, x, y);
}

void Display::drawCenteredText(const char* text, uint16_t color, uint8_t fontSize) {
    sprite_.setTextColor(color, TFT_BLACK);
    sprite_.setTextSize(fontSize);
    sprite_.setTextDatum(MC_DATUM); // Middle-Center — спільний для TFT_eSPI і LovyanGFX
    sprite_.drawString(text, width() / 2, height() / 2);
    sprite_.setTextDatum(TL_DATUM); // повертаємо датум за замовчуванням
}

void Display::flush() {
    sprite_.pushSprite(0, 0);
}

int Display::width() const {
    return width_;
}
 
int Display::height() const {
    return height_;
}
