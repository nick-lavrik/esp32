// Display.cpp
#include <stdarg.h> // Обов'язково для роботи з трикрапкою (...)
#include "Display.h"

// Глобальний об'єкт "tft" створюється рівно в одному файлі на середовище:
//   env:esp32-st7789      -> src/TftInstance_ST7789.cpp
//   env:esp32-4848s040    -> src/TftInstance_4848S040.cpp
// Тут ми лише беремо на нього посилання.

Display::Display() : tft_(tft), sprite_(&tft_) {}

void Display::init() {
    #if defined(BOARD_ST7789)
    pinMode(TFT_BL, OUTPUT); // st7789
    #endif

    tft_.init();
    tft_.setRotation(TFT_ROTATION);

    width_  = tft_.width();
    height_ = tft_.height();

    // Спрайт на весь розмір екрана — вся подальша робота йде через нього.
    sprite_.setColorDepth(SPRITE_COLOR_DEPTH); // 16
    sprite_.setSwapBytes(true);
    sprite_.createSprite(width_, height_);
    void* buf = sprite_.createSprite(width_, height_);
    if (buf == nullptr) {
        Serial.println("[Display] ПОМИЛКА: createSprite() не зміг виділити пам'ять!");
        Serial.printf("[Display] Потрібно: %d байт, вільно (heap): %u байт\n", width_ * height_ * 2, ESP.getFreeHeap());
    }

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
    sprite_.setTextColor(color, TFT_TRANSPARENT);
    sprite_.setTextSize(fontSize);
    sprite_.setTextDatum(MC_DATUM); // Middle-Center — спільний для TFT_eSPI і LovyanGFX
    sprite_.drawString(text, width() / 2, height() / 2);
    sprite_.setTextDatum(TL_DATUM); // повертаємо датум за замовчуванням
}

void Display::setCursor(int32_t x, int32_t y) {
    sprite_.setCursor(x, y);
}

void Display::pushImage(int32_t x, int32_t y, int32_t w, int32_t h, const uint16_t *data) {
    sprite_.pushImage(x, y, w, h, data);
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

void Display::brightness(uint8_t percent) {
    percent = percent < 0 ? 0 : percent;
    percent = percent > 100 ? 100 : percent;

    // if (brightness_ == percent) return;

    #if defined(BOARD_ST7789)
    analogWrite(TFT_BL, map(percent, 0, 100, 0, 255));
    #endif

    #if defined(BOARD_4848S040)
    tft_.setBrightness(map(percent, 0, 100, 15, 255)); // делегуємо в LGFX Light_PWM, пін вже сконфігурований у Setup_ST7701_4848S040.h
    #endif

    brightness_ = percent;
}

const uint32_t Display::loopFrameRate() {
    // --- Метрики "здоров'я" системи ---
    static uint32_t loopCounter = 0;
    static uint32_t loopsPerSecond = 0;
    static uint32_t lastLoopCheckMs = 0;

    loopCounter++;

    uint32_t now = millis();

    // Підрахунок швидкості циклів loop() за секунду
    if (now - lastLoopCheckMs >= 1000) {
      loopsPerSecond = loopCounter;
      loopCounter = 0;
      lastLoopCheckMs = now;
    }

    return loopsPerSecond;
}
