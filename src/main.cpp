// main.cpp
//
// Працює однаково для обох середовищ, різниться лише build_flags (-include)
// у platformio.ini:
//   env:esp32-st7789      -> include/Setup_ST7789.h        (bodmer/TFT_eSPI, SPI)
//   env:esp32-4848s040    -> include/Setup_ST7701_4848S040.h (LovyanGFX, RGB-панель)

#include <Arduino.h>
#include "Display.h"
#include <SPI.h>
#include "wifi.h"
#include "ntp.h"
#include "ping.h"

Display display;
//#include <TFT_eSPI.h>
//TFT_eSPI tft = TFT_eSPI();

#include "BackgroundImages.h"

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

void setupSerial() {
    Serial.begin(115200);
    Serial.println("Hello, ESP32!");
}

void setupDisplay() {
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
    // display.drawCenteredText("Hello, ESP32!", TFT_YELLOW, 4);
}

void setup() {
    setupSerial();
    setupDisplay();
    setupWiFi();
    setupNtpService();
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


void drawSystemInfo() {
  // img.fillRect(0, 30, 320, 65, BG_COLOR);

  uint32_t freeHeap = ESP.getFreeHeap();
  uint32_t totalHeap = ESP.getHeapSize();
  int heapPercent = (freeHeap * 100) / totalHeap;

  uint32_t cpuFreq = ESP.getCpuFreqMHz();
  uint32_t uptimeSec = millis() / 1000;

  display.setTextSize(1);
  display.setTextColor(TFT_DARKGREY);

  display.setCursor(10, 10 + 0 * (5 +  display.fontHeight()));
  display.printf("Uptime: %02d:%02d:%02d", uptimeSec / 3600, (uptimeSec / 60) % 60, uptimeSec % 60);

  display.setCursor(10, 10 + 1 * (5 +  display.fontHeight()));
  display.printf("CPU: %d MHz   Loop rate: %d/s", cpuFreq, loopFrameRate());

  display.setCursor(10, 10 + 2 * (5 +  display.fontHeight()));
  display.printf("Heap free: %d KB / %d KB (%d%%)", freeHeap / 1024, totalHeap / 1024, heapPercent);

  display.setCursor(10, 10 + 3 * (5 +  display.fontHeight()));
  display.print(dumpPingStatsStr());

  // Візуальний бар пам'яті
  // int barX = 10, barY = 56, barW = 300, barH = 10;
  // img.drawRect(barX, barY, barW, barH, GRID_COLOR);
  // int fillW = (heapPercent * (barW - 2)) / 100;
  // uint16_t barColor = heapPercent > 30 ? TFT_GREEN : (heapPercent > 15 ? TFT_YELLOW : TFT_RED);
  // img.fillRect(barX + 1, barY + 1, fillW, barH - 2, barColor);

  // int lightPercent = readLightPercent();
  // img.setCursor(180, 70);
  // img.printf("Light: %d%%", lightPercent);
}

void loop() {
    doPing();

    display.clear();

    drawBackgroundImage();
    drawSystemInfo();
    drawTime();

    display.flush();
    delay(16);
}
