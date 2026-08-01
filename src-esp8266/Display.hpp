// Display.hpp
#pragma once

#include <stdarg.h> // Обов'язково для роботи з трикрапкою (...)
#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// Монохромний I2C OLED (SSD1306, звичайно 128x64) на NodeMCU ESP8266.
//
// Публічний інтерфейс навмисно повторює підмножину src/Display.h (яка
// використовується у wifi.h/ntp.h/main.cpp), щоб той самий прикладний
// код (setupWiFi/drawTime/drawSystemInfo) працював без змін.
//
// ВАЖЛИВО: екран монохромний (лише 2 кольори), тому будь-який "колір"
// (TFT_WHITE/TFT_RED/TFT_YELLOW/...) з боку прикладного коду мапиться
// у SSD1306_WHITE або SSD1306_BLACK - див. OledColors.hpp.
class Display {
public:
    explicit Display();

    // Ініціалізація дисплея (обов'язково викликати в setup())
    void init();

    // Заливка всього екрану кольором (0 = BLACK, інше = WHITE)
    void clear(uint16_t color = 0);

    // Малювання тексту в позиції (x, y)
    void drawText(int x, int y, const char* text, uint16_t color);

    // Малювання тексту по центру екрана
    void drawCenteredText(const char* text, uint16_t color, uint8_t fontSize = 1);

    // Виводить накопичений кадр (framebuffer) на реальний екран.
    void flush();

    uint8_t brightness() { return brightness_; } // percent!
    void brightness(uint8_t percent);            // SSD1306 контраст (0-100 -> 0-255)

    void setCursor(int32_t x, int32_t y);

    int width() const;
    int height() const;

    const uint32_t loopFrameRate();
    size_t fontHeight() { return 8 * textSize_; } // вбудований шрифт Adafruit_GFX: 8px комірка

    void setTextFont(uint8_t) { /* немає альтернативних шрифтів на SSD1306 - no-op */ }
    void setTextColor(uint16_t color)              { oled_.setTextColor(color ? SSD1306_WHITE : SSD1306_BLACK); }
    void setTextColor(uint16_t color, uint16_t bg)  { oled_.setTextColor(color ? SSD1306_WHITE : SSD1306_BLACK, bg ? SSD1306_WHITE : SSD1306_BLACK); }
    void setTextSize(uint8_t size)                  { textSize_ = size; oled_.setTextSize(size); }

    void     drawRect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color) { oled_.drawRect(x, y, w, h, color ? SSD1306_WHITE : SSD1306_BLACK); }
    void     drawCircle(int32_t x, int32_t y, int32_t r, uint32_t color)          { oled_.drawCircle(x, y, r, color ? SSD1306_WHITE : SSD1306_BLACK); }
    uint16_t drawString(const char *text, int32_t x, int32_t y);

    int16_t  textWidth(const char *string);

    size_t print(const char *string)   { return oled_.print(string); }
    size_t println(const char *string) { return oled_.println(string); }

    template <typename... Args>
    size_t printf(const __FlashStringHelper *ifsh, const Args&... args) {
        return oled_.printf(reinterpret_cast<const char*>(ifsh), args...);
    }

    template <typename... Args>
    size_t printf(const char* format, const Args&... args) {
        return oled_.printf(format, args...);
    }

private:
    Adafruit_SSD1306 oled_;

    int width_  = 0;
    int height_ = 0;
    uint8_t brightness_ = 100; // percent!
    uint8_t textSize_ = 1;
};
