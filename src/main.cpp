// main.cpp
//
// Працює однаково для обох середовищ, різниться лише build_flags (-include)
// у platformio.ini:
//   env:esp32-st7789      -> include/Setup_ST7789.h        (bodmer/TFT_eSPI, SPI)
//   env:esp32-4848s040    -> include/Setup_ST7701_4848S040.h (LovyanGFX, RGB-панель)

#include <Arduino.h>
#include "Display.h"
#include <SPI.h>
#include <LittleFS.h>
#include "wifi.h"
#include "ntp.h"
#include "ping.h"
#include "GmailSender.h"
#include "JpegImage.h"
#include "HeapMonitor.h"

Display display;

#include "BackgroundImages.h"

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

static TouchScreenConfig makeTouchScreenConfig() {
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

TouchScreenConfig touchScreenConfig = makeTouchScreenConfig();
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

#if HAS_GMAIL_SENDER
GmailSender mailer(GMAIL_EMAIL, GMAIL_PASSWORD, "ESP32 Device");
#endif

void setupLittleFS() {

  if (!LittleFS.begin(true)) {
    Serial.println("LittleFS mount failed!");
  } else {
    Serial.println("LittleFS mounted successfully (done)");
  }

  delay(50);

  // --- Список усіх файлів ---
  File root = LittleFS.open("/");
  File file = root.openNextFile();
  while (file) {
    Serial.printf("File: %s, size: %d bytes\n", file.name(), file.size());
    file = root.openNextFile();
  }

  size_t usedBytes = LittleFS.usedBytes();
  size_t totalBytes = LittleFS.totalBytes();
  double freePercent = ((totalBytes - usedBytes) * 100.00 / totalBytes);

  // --- Скільки місця залишилось ---
  Serial.printf("Used: %d / Total: %d / Free: %d bytes | Free: %.3f%%\n", usedBytes, totalBytes, totalBytes - usedBytes, freePercent);
}

JpegImage spaceImage;

void dumpChipsetInfo() {
    static uint32_t lastDumpMs = millis() - 5 * 60 * 1000 - 10;
    if ((millis() - lastDumpMs) < 5 * 60 * 1000) return;
    lastDumpMs = millis();

    Serial.println("\n===== ESP32 CHIP INFO =====");

    // --- Модель чипа ---
    Serial.printf("Chip model: %s\n", ESP.getChipModel());
    Serial.printf("Chip revision: %d\n", ESP.getChipRevision());
    Serial.printf("CPU cores: %d\n", ESP.getChipCores());
    Serial.printf("CPU freq: %d MHz\n", ESP.getCpuFreqMHz());

    // --- Flash ---
    Serial.printf("Flash size: %d bytes (%.2f MB)\n", ESP.getFlashChipSize(), ESP.getFlashChipSize() / 1024.0 / 1024.0);
    Serial.printf("Flash speed: %d Hz\n", ESP.getFlashChipSpeed());

    // --- Внутрішня RAM (SRAM) ---
    Serial.printf("Total heap: %d bytes\n", ESP.getHeapSize());
    Serial.printf("Free heap: %d bytes\n", ESP.getFreeHeap());

    // --- PSRAM ---
    Serial.printf("PSRAM found: %s\n", psramFound() ? "YES" : "NO");
    if (psramFound()) {
        Serial.printf("Total PSRAM: %d bytes (%.2f MB)\n", ESP.getPsramSize(), ESP.getPsramSize() / 1024.0 / 1024.0);
        Serial.printf("Free PSRAM: %d bytes\n", ESP.getFreePsram());
    }

    /* Serial.println("\n==== ESP32 HEAP INFO =====");
    heap_caps_print_heap_info(MALLOC_CAP_DEFAULT); // друкує все одразу у форматованому вигляді */
    Serial.println("============================");
}

HeapMonitor heapMonitor(60000); // друк кожні 60 сек

void setup() {
    setupSerial();
    heapMonitor.begin();
    dumpChipsetInfo();
    setupDisplay();
    setupLittleFS();
    setupTouchScreen();
    setupWiFi();
    setupNtpService();

    #if SPRITE_COLOR_DEPTH == 16
    spaceImage.loadFromLittleFS("/space-01.jpg", JpegColorDepth::RGB565);
    setBackgroundImage(spaceImage);
    #elif SPRITE_COLOR_DEPTH == 8
    //spaceImage.loadFromLittleFS("/space-03.jpg", JpegColorDepth::RGB332);
    //setBackgroundImage(spaceImage);
    #endif

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

void sendEmail() {
    static bool once = false;
    if (once) {
        return;
    }

    once = true;
    display.drawText(10, 10 + 3 * (3 + display.fontHeight()), "SMTP sendmail", TFT_LIGHTGREY);
    display.flush();
    #if HAS_GMAIL_SENDER
    //mailer.sendEmail("nick.lavrik@gmail.com", PIO_PIOENV, "hhhh");
    #endif
    display.drawText(10, 10 + 4 * (3 + display.fontHeight()), "SMTP sendmail (done)", TFT_LIGHTGREY);
    display.flush();
}

void loop() {
    heapMonitor.update(); // неблокуюче, друкує тільки коли настав час
    doPing();

    display.clear();
    touchController.update();
    dumpChipsetInfo();
    drawBackgroundImage();
    drawSystemInfo();
    drawPoints();
    drawTime();
    sendEmail();

    display.flush();
    delay(16);
}
