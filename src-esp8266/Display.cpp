// Display.cpp
#include "Display.hpp"

// SSD1306 контролери зазвичай не мають апаратного I2C-скидання -> OLED_RESET_PIN=-1
Display::Display() : oled_(OLED_WIDTH, OLED_HEIGHT, &Wire, OLED_RESET_PIN) {}

void Display::init() {
    width_  = OLED_WIDTH;
    height_ = OLED_HEIGHT;

    Wire.begin(); // NodeMCU: SDA=D2 (GPIO4), SCL=D1 (GPIO5) - типова розводка модуля

    if (!oled_.begin(SSD1306_SWITCHCAPVCC, OLED_I2C_ADDR)) {
        Serial.println("[Display] ПОМИЛКА: SSD1306.begin() не вдався (перевір I2C адресу/проводку)");
        return;
    }

    oled_.cp437(true);
    oled_.setTextColor(SSD1306_WHITE);
    oled_.setTextSize(1);
    oled_.setTextWrap(false);
    oled_.clearDisplay();

    brightness(brightness_);
    flush(); // одразу показуємо чорний кадр
}

void Display::clear(uint16_t color) {
    if (color == 0) {
        oled_.clearDisplay();
    } else {
        oled_.fillScreen(SSD1306_WHITE);
    }
    oled_.setCursor(0, 0);
}

void Display::drawText(int x, int y, const char* text, uint16_t color) {
    setTextColor(color);
    oled_.setCursor(x, y);
    oled_.print(text);
}

void Display::drawCenteredText(const char* text, uint16_t color, uint8_t fontSize) {
    setTextSize(fontSize);
    setTextColor(color);

    int16_t x1, y1;
    uint16_t w, h;
    oled_.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);

    int x = (width() - static_cast<int>(w)) / 2;
    int y = (height() - static_cast<int>(h)) / 2;
    oled_.setCursor(x, y);
    oled_.print(text);
}

void Display::setCursor(int32_t x, int32_t y) {
    oled_.setCursor(x, y);
}

void Display::flush() {
    oled_.display();
}

int Display::width() const {
    return width_;
}

int Display::height() const {
    return height_;
}

void Display::brightness(uint8_t percent) {
    percent = percent > 100 ? 100 : percent;

    // SSD1306 не має регулювання яскравості підсвітки (немає підсвітки як такої) -
    // єдина доступна ручка - контраст пікселів (0..255).
    oled_.ssd1306_command(SSD1306_SETCONTRAST);
    oled_.ssd1306_command(map(percent, 0, 100, 0, 255));

    brightness_ = percent;
}

uint16_t Display::drawString(const char *text, int32_t x, int32_t y) {
    oled_.setCursor(x, y);
    oled_.print(text);
    return textWidth(text);
}

int16_t Display::textWidth(const char *string) {
    int16_t x1, y1;
    uint16_t w, h;
    oled_.getTextBounds(string, 0, 0, &x1, &y1, &w, &h);
    return static_cast<int16_t>(w);
}

const uint32_t Display::loopFrameRate() {
    static uint32_t loopCounter = 0;
    static uint32_t loopsPerSecond = 0;
    static uint32_t lastLoopCheckMs = 0;

    loopCounter++;

    uint32_t now = millis();
    if (now - lastLoopCheckMs >= 1000) {
        loopsPerSecond = loopCounter;
        loopCounter = 0;
        lastLoopCheckMs = now;
    }

    return loopsPerSecond;
}
