// Display.cpp
#include "Display.h"

// Глобальний об'єкт "tft" створюється рівно в одному файлі на середовище:
//   env:esp32-st7789      -> src/TftInstance_ST7789.cpp
//   env:esp32-4848s040    -> src/TftInstance_4848S040.cpp
// Тут ми лише беремо на нього посилання.

Display::Display() : tft_(tft) {}

void Display::init() {
    Serial.println("Display::init()"); delay(100);
    tft_.init();
    Serial.println("tft.init() ok"); delay(100);
    tft_.setRotation(TFT_ROTATION);
    Serial.println("tft.setRoration() ok"); delay(100);
    tft_.fillScreen(TFT_BLACK);
    Serial.println("tft.fillScreen() ok"); delay(100);
}

void Display::clear(uint16_t color) {
    tft_.fillScreen(color);
}

void Display::drawText(int x, int y, const char* text, uint16_t color) {
    tft_.setTextColor(color);
    tft_.drawString(text, x, y);
}

void Display::drawCenteredText(const char* text, uint16_t color, uint8_t fontSize) {
    tft_.setTextColor(color, TFT_BLACK);
    tft_.setTextSize(fontSize);
    tft_.setTextDatum(MC_DATUM); // Middle-Center — спільний для TFT_eSPI і LovyanGFX
    tft_.drawString(text, width() / 2, height() / 2);
    tft_.setTextDatum(TL_DATUM); // повертаємо датум за замовчуванням
}

int Display::width() const {
    return tft_.width();
}

int Display::height() const {
    return tft_.height();
}