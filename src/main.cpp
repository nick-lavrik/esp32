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

#include "TouchController.h"
#include "TouchEvents.h"

TouchController touchController;

static TouchScreenConfig makeMapperConfig() {
    TouchScreenConfig c;
    // Приклад: контролер видає сирі 0..4095, екран фізично 320x240,
    // а сама панель ще й повернута (типова ситуація для дешевих SPI TFT).
    // c.rawMinX = 200;  c.rawMaxX = 3900; // підбирається калібруванням
    // c.rawMinY = 200;  c.rawMaxY = 3900;

    #ifdef BOARD_ST7789
    c.screenWidth  = 320;
    c.screenHeight = 240;

    c.invertY = true;   // якщо вертикаль перевернута
    c.invertX = true;   // якщо горизонталь перевернута
    c.swapXY = false;  // якщо екран повернутий на 90/270 градусів
    #endif

    #ifdef BOARD_4848S040
    c.screenWidth  = 480;
    c.screenHeight = 480;
    #endif

    return c;
}

TouchScreenConfig touchScreenConfig = makeMapperConfig();
TouchPointMapper mapper(touchScreenConfig);
TouchEvents touch(touchScreenConfig);

void onTouchLog(TouchPoint p)                              { Serial.printf("Touch: %d, %d\n", p.x, p.y); }
void onHoldHandler(TouchPoint p, unsigned long ms)         { Serial.printf("Hold at %d,%d for %lu ms\n", p.x, p.y, ms); }
void onDblClickHandler(TouchPoint p)                       { Serial.printf("Double click: %d, %d\n", p.x, p.y); }

void onSwipeLeftHandler(TouchPoint start, TouchPoint end)  { Serial.println("Swipe LEFT"); }
void onSwipeRightHandler(TouchPoint start, TouchPoint end) { Serial.println("Swipe RIGHT"); }
void onSwipeUpHandler(TouchPoint start, TouchPoint end)    { Serial.println("Swipe UP"); }
void onSwipeDownHandler(TouchPoint start, TouchPoint end)  { Serial.println("Swipe DOWN"); }

void onSwipeFromBottomHandler(TouchPoint start, TouchPoint end) { Serial.println("Swipe FROM BOTTOM (напр., відкрити меню)"); }
void onSwipeFromTopHandler(TouchPoint start, TouchPoint end)    { Serial.println("Swipe FROM TOP (напр., шторка сповіщень)"); }
void onSwipeFromLeftHandler(TouchPoint start, TouchPoint end)   { Serial.println("Swipe FROM LEFT (напр., назад)"); }
void onSwipeFromRightHandler(TouchPoint start, TouchPoint end)  { Serial.println("Swipe FROM RIGHT (напр., бокова панель)"); }

void setupTouchScreen() {
    #ifdef BOARD_ST7789
    touch.setTouchPointMapper(&mapper);
    #endif
    touchController.setup(&touch);
    touchController.events().onTouch(onTouchLog);
    touchController.events().onHold(onHoldHandler);
    touchController.events().onDblClick(onDblClickHandler);
    touchController.events().onSwipeLeft(onSwipeLeftHandler);
    touchController.events().onSwipeRight(onSwipeRightHandler);
    touchController.events().onSwipeUp(onSwipeUpHandler);
    touchController.events().onSwipeDown(onSwipeDownHandler);
    touchController.events().onSwipeFromBottom(onSwipeFromBottomHandler);
    touchController.events().onSwipeFromTop(onSwipeFromTopHandler);
    touchController.events().onSwipeFromLeft(onSwipeFromLeftHandler);
    touchController.events().onSwipeFromRight(onSwipeFromRightHandler);
}

void setup() {
    setupSerial();
    setupDisplay();
    setupTouchScreen();
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

void drawPoints() {
    display.drawRect(0, 0, 1, 1, TFT_WHITE);
    display.drawRect(display.width()-1, 0, 1, 1, TFT_WHITE);
    display.drawRect(display.width()-1, display.height() - 1, 1, 1, TFT_WHITE);
    display.drawRect(0, display.height() - 1, 1, 1, TFT_WHITE);
}

void loop() {

    doPing();

    display.clear();
    touchController.update();

    drawBackgroundImage();
    drawSystemInfo();
    drawPoints();
    drawTime();

    display.flush();
    delay(16);
}
