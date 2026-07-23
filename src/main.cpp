// main.cpp
//
// Працює однаково для обох середовищ, різниться лише build_flags (-include)
// у platformio.ini:
//   env:esp32-st7789      -> include/Setup_ST7789.h        (bodmer/TFT_eSPI, SPI)
//   env:esp32-4848s040    -> include/Setup_ST7701_4848S040.h (LovyanGFX, RGB-панель)

#include <Arduino.h>
#include "Display.h"
#include <SPI.h>

Display display;
//#include <TFT_eSPI.h>
//TFT_eSPI tft = TFT_eSPI();

#include "BackgroundImages.h"

size_t currentBackgroundImageIndex = 0;

const uint16_t my_colors[] = {
    TFT_RED,
    TFT_GREEN,
    TFT_BLUE,
    TFT_WHITE
};

static uint16_t currentColor = TFT_RED;
const int total_colors = sizeof(my_colors) / sizeof(my_colors[0]);

/**
 * Функция возвращает следующий цвет из массива.
 * @param current_color Текущий цвет, который активен сейчас.
 * @return uint16_t Следующий цвет по кругу.
 */
uint16_t get_next_color(uint16_t current_color) {
    // 1. Ищем индекс текущего цвета в нашем массиве
    for (int i = 0; i < total_colors; i++) {
        if (my_colors[i] == current_color) {
            // 2. Нашли! Вычисляем индекс следующего элемента.
            // Оператор % (остаток от деления) сбросит индекс на 0, если дошли до конца.
            int next_index = (i + 1) % total_colors;
            return my_colors[next_index];
        }
    }
    
    // Если текущий цвет не найден в массиве (например, при первом запуске),
    // возвращаем самый первый цвет по умолчанию (TFT_RED)
    return my_colors[0];
}

void setup() {

    Serial.begin(115200);
    Serial.println("Hello, ESP32!");

    #ifdef BOARD_4848S040
    pinMode(TFT_BL, OUTPUT);
    analogWrite(TFT_BL, 80);
    #endif
    
    #ifdef BOARD_ST7789
    pinMode(TFT_BL, OUTPUT);
    analogWrite(TFT_BL, 80);
    #endif

    display.init();
    display.clear(TFT_BLACK);
    display.drawCenteredText("Hello, ESP32!", TFT_YELLOW, 4);

    display.flush();
}

const uint32_t loopFrameRate() {
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

void loop() {

    display.clear();
    currentBackgroundImageIndex = nextBackgroundIndex(currentBackgroundImageIndex);
    // display.pushImage(0, 0, display.width(), display.height(), backgroundImages[currentBackgroundImageIndex]);
    display.pushImage((uint32_t) (display.width() - 320) / 2, (uint32_t) (display.height() - 240) / 2, 320, 240, backgroundImages[currentBackgroundImageIndex]);

    // нічого не робимо — напис вже намальовано в setup()
    // display.drawCenteredText("Hello, ESP32!", currentColor, 4);
    display.setTextColor(currentColor);
    display.setTextSize(4);
    
    display.setCursor((display.width() - display.textWidth("Hello, ESP321")) / 2, (display.height() - display.fontHeight()) / 2);
    display.printf("Hello, ESP32!");
    currentColor = get_next_color(currentColor);

    display.setCursor(10, 32);
    display.setTextSize(2);
    display.printf("frame rate: %d/s", loopFrameRate());

    display.flush();
    delay(1000);
}
