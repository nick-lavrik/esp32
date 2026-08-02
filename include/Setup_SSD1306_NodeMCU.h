// Setup_SSD1306_NodeMCU.h
// TFT_eSPI-сумісний шар для монохромного I2C OLED SSD1306 (NodeMCU ESP8266).
//
// bodmer/TFT_eSPI підтримує лише SPI/паралельні кольорові панелі, а
// LovyanGFX (як для 4848S040) теж розрахований на кольорові дисплеї,
// тому тут - власна мінімальна TFT_eSPI/TFT_eSprite-сумісна обгортка
// над Adafruit_SSD1306/Adafruit_GFX, щоб src/Display.h та src/Display.cpp
// (спільний прикладний код для ВСІХ плат) лишались без змін.
//
// Підключається через src/TftInstance.h за BOARD_ESP8266 (аналогічно тому,
// як Setup_ST7701_4848S040.h підключається за BOARD_4848S040).
//
// ВАЖЛИВО: екран монохромний (1 біт на піксель) - SPRITE_COLOR_DEPTH=1,
// а всі TFT_* кольори нижче мають лише 2 реальні стани (0/1).

#pragma once

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// Кольори (SSD1306_BLACK=0 / SSD1306_WHITE=1 - тому конвертація "кольору"
// у монохромний піксель звичайним booleanʼом (color != 0) без табличного мапінгу)
#define TFT_BLACK       0
#define TFT_WHITE       1
#define TFT_RED         1
#define TFT_GREEN       1
#define TFT_YELLOW      1
#define TFT_CYAN        1
#define TFT_ORANGE      1
#define TFT_LIGHTGREY   1
#define TFT_DARKGREY    1
#define TFT_TRANSPARENT 0

// Датуми тексту (підмножина TFT_eSPI, якої вистачає src/Display.cpp)
#define TL_DATUM 0
#define MC_DATUM 4

// "Пристрій" - сам OLED. Публічний API - підмножина bodmer/TFT_eSPI,
// якою користується src/Display.h (init/setRotation/getRotation/width/
// height/startWrite/endWrite).
class TFT_eSPI : public Adafruit_SSD1306 {
public:
    TFT_eSPI() : Adafruit_SSD1306(OLED_WIDTH, OLED_HEIGHT, &Wire, OLED_RESET_PIN) {}

    void init() {
        Wire.begin(); // NodeMCU: SDA=D2(GPIO4), SCL=D1(GPIO5) - типова розводка модуля

        if (!begin(SSD1306_SWITCHCAPVCC, OLED_I2C_ADDR)) {
            Serial.println(F("[TFT_eSPI/SSD1306] begin() failed - перевір I2C адресу/проводку"));
            return;
        }

        cp437(true);
        clearDisplay();
        display();
    }

    // Adafruit_GFX не має getRotation() - зберігаємо стан самі
    void setRotation(uint8_t r) { Adafruit_SSD1306::setRotation(r); _rotation = r; }
    uint8_t getRotation() const { return _rotation; }

    // SSD1306 не має пакетної транзакції запису як TFT_eSPI - no-op
    void startWrite() {}
    void endWrite() {}

private:
    uint8_t _rotation = 0;
};

/**
 * єдиний глобальний екземпляр, визначений в
 * @see file://./../src-esp8266/TftInstance.cpp
 */
extern TFT_eSPI tft;

// "Спрайт" - для монохромного SSD1306 окремого буфера кадру не потрібно:
// Adafruit_SSD1306 вже є власним framebuffer'ом, тож TFT_eSprite малює
// напряму в той самий буфер, що й tft (без подвійного виділення пам'яті).
class TFT_eSprite : public Adafruit_GFX {
public:
    explicit TFT_eSprite(TFT_eSPI* tft) : Adafruit_GFX(OLED_WIDTH, OLED_HEIGHT), _tft(tft) {}

    void drawPixel(int16_t x, int16_t y, uint16_t color) override {
        _tft->drawPixel(x, y, color ? SSD1306_WHITE : SSD1306_BLACK);
    }

    // Монохромний екран - немає ні глибини кольору, ні порядку байтів
    void setColorDepth(uint8_t) {}
    void setSwapBytes(bool) {}

    // Буфер вже існує всередині tft_ - виділяти нічого не треба,
    // достатньо повернути "не nullptr", щоб Display::init() не впав в помилку.
    void* createSprite(int32_t, int32_t) { return static_cast<void*>(this); }

    void fillSprite(uint16_t color) { _tft->fillScreen(color ? SSD1306_WHITE : SSD1306_BLACK); }
    void pushSprite(int32_t, int32_t) { _tft->display(); }

    // Кольорові (RGB565) зображення на монохромному екрані не мають сенсу -
    // цей метод не використовується для NodeMCU+OLED (BackgroundImages
    // виключені з build_src_filter для env:esp8266), лишений лише для
    // сумісності сигнатури з src/Display.h.
    void pushImage(int32_t, int32_t, int32_t, int32_t, const uint16_t*) {}

    size_t fontHeight() { return 8 * textsize; } // вбудований шрифт Adafruit_GFX: комірка 8px по висоті
    void setTextFont(uint8_t) {}                 // альтернативних шрифтів на SSD1306 немає - no-op
    void setTextDatum(uint8_t datum) { _datum = datum; }

    int16_t textWidth(const char* text) {
        int16_t x1, y1; uint16_t w, h;
        getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
        return static_cast<int16_t>(w);
    }

    uint16_t drawString(const char* text, int32_t x, int32_t y) {
        int16_t x1, y1; uint16_t w, h;
        getTextBounds(text, 0, 0, &x1, &y1, &w, &h);

        if (_datum == MC_DATUM) {
            x -= static_cast<int32_t>(w) / 2;
            y -= static_cast<int32_t>(h) / 2;
        }

        setCursor(x, y);
        print(text);
        return static_cast<uint16_t>(w);
    }

    // Делегуємо до успадкованого Print::printf(const char*, ...) - так само,
    // як src/Display.h робить для bodmer/TFT_eSPI та LGFX_Sprite.
    template <typename... Args>
    size_t printf(const __FlashStringHelper* ifsh, const Args&... args) {
        return this->printf(reinterpret_cast<const char*>(ifsh), args...);
    }

private:
    TFT_eSPI* _tft;
    uint8_t _datum = TL_DATUM;
};
