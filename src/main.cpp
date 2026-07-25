// main.cpp
//
// Працює однаково для обох середовищ, різниться лише build_flags (-include)
// у platformio.ini:
//   env:esp32-st7789      -> include/Setup_ST7789.h        (bodmer/TFT_eSPI, SPI)
//   env:esp32-4848s040    -> include/Setup_ST7701_4848S040.h (LovyanGFX, RGB-панель)

// static const uint32_t freqs[] = {150, 300, 500, 800, 1000, 2000};
// for (uint32_t f : freqs) {
//     Serial.printf("Testing freq = %u Hz\n", f);
//     // На жаль, LGFX Light_PWM не дає змінити freq в рантаймі без
//     // повторної ініціалізації - тому цей тест краще робити,
//     // міняючи light_cfg.freq в Setup_ST7701_4848S040.h і перепрошиваючи,
//     // а не в рантаймі.
// }

// ===== ESP32 CHIP INFO =====
// PlatformIO: esp32-4848s040
// Chip model: ESP32-S3
// Chip revision: 2
// CPU cores: 2
// CPU freq: 240 MHz
// SDK version:  v5.5.4
// Core version: 3.3.9
// ===== ESP32 CHIP INFO =====
// PlatformIO: esp32-st7789
// Chip model: ESP32-D0WD-V3
// Chip revision: 301
// CPU cores: 2
// CPU freq: 240 MHz
// SDK version:  v5.5.4
// Core version: 3.3.9

#include <Arduino.h>
#include "Display.h"
#include <SPI.h>
#include <LittleFS.h>
#include "wifi.h"
#include "ntp.h"
#include "ping.h"
#include "GmailSender.hpp"
#include "JpegImage.hpp"
#include "SerialCommandHandler.hpp"
#include "SystemReset.hpp"
#include "BackgroundImages.hpp"
#include "ConfigStorage.hpp"
#include "TouchScreen/TouchController.h"
#include "TaskController/TaskController.hpp"
#include <EventDispatcher.hpp>
#include <Event.hpp>

void setupSerial() {
    Serial.begin(115200);
    delay(200);
    Serial.printf("\n\n-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*\n");
    Serial.printf(" %s (%s)\n", ESP.getChipModel(), PIO_PIOENV );
    Serial.printf("-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*\n\n");
}

// --- Підсвітка + світловий сенсор ---
// #define LIGHT_SENSOR_PIN 34

void setupDisplay() {
    display.init();
    display.autobrightness(true);
    Serial.println("Display setup done.");
}

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
    c.edgeZoneX = 40;
    c.edgeZoneY = 40;
    #endif

    return c;
}

EventDispatcher dispatcher;
Display display(&dispatcher);
TaskController scheduler;
TouchController touchController;
TouchScreenConfig touchScreenConfig = makeTouchScreenConfig();
TouchPointMapper mapper(touchScreenConfig);
TouchEvents touch(touchScreenConfig);
ConfigStorage configStorage;
JpegImage spaceImage;

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

void onHoldDrawPoints(TouchPoint p, unsigned long ms) {
    display.autobrightness(true);

    // Тип 2 (JobTask): "показувати frame"
    // постійно протягом 1 хвилини, після чого само зникає з черги
    scheduler.addJob(
        10UL * 1000UL,
        [p]() {
            display.drawCircle(p.x, p.y, 4, TFT_YELLOW);
            display.drawRect(0, 0, 1, 1, TFT_WHITE);
            display.drawRect(display.width()-1, 0, 1, 1, TFT_WHITE);
            display.drawRect(display.width()-1, display.height() - 1, 1, 1, TFT_WHITE);
            display.drawRect(0, display.height() - 1, 1, 1, TFT_WHITE);

            display.drawRect(
                touchScreenConfig.edgeZoneX, touchScreenConfig.edgeZoneY,
                touchScreenConfig.screenWidth - 2 * touchScreenConfig.edgeZoneX,
                touchScreenConfig.screenHeight - 2 * touchScreenConfig.edgeZoneY,
                TFT_DARKGREY
            );
        },
        1 // з інтервалом 100 мс, а не на кожному tick()
    );

    Serial.printf("\nONHOLD FRAME !!!\n\n");
}

void setupTouchScreen() {

    #ifdef BOARD_ST7789
    touch.setTouchPointMapper(&mapper);
    #endif

    touchController.setup(&touch);
    Serial.println("TouchScreen setup done");

    touchController.events().onHold(onHoldDrawPoints);

    touchController.events().onSwipeUp([](TouchPoint s, TouchPoint e) {
        display.autobrightness(false);
        if (display.brightness() == 0) {
            display.brightness(1);
        } else if (display.brightness() == 1) {
            display.brightness(10);
        } else {
            display.brightness(min(display.brightness() + 10, 255)); 
        }
        Serial.printf("Brightness: %d%% (increase)\n", display.brightness());
    });

    touchController.events().onSwipeDown([](TouchPoint s, TouchPoint e) { 
        display.autobrightness(false);
        if (display.brightness() == 1) {
            display.brightness(0);
        } else {
            display.brightness(max(display.brightness() - 10, 1)); 
        }
        Serial.printf("Brightness: %d%% (decrease)\n", display.brightness());
    });

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

    Serial.println("TouchScreen controller done");
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
}

void dumpChipsetInfo() {
    /* static uint32_t lastDumpMs = millis() - 5 * 60 * 1000 - 10;
    if ((millis() - lastDumpMs) < 5 * 60 * 1000) return;
    lastDumpMs = millis(); */

    Serial.println("\n===== ESP32 CHIP INFO =====");

    // --- PlatformIO environment ---
    Serial.printf("PlatformIO: %s\n", PIO_PIOENV);

    // --- Модель чипа ---
    Serial.printf("Chip model: %s\n", ESP.getChipModel());
    Serial.printf("Chip revision: %d\n", ESP.getChipRevision());
    Serial.printf("CPU cores: %d\n", ESP.getChipCores());
    Serial.printf("CPU freq: %d MHz\n", ESP.getCpuFreqMHz());

    //  возвращает общее количество тактов процессора (CPU cycles), прошедших с момента запуска
    // Serial.printf("Cycle Count: %d\n", ESP.getCycleCount());

    // --- ESP-IDF ---
    Serial.printf("SDK version:  %s\n", ESP.getSdkVersion());
    Serial.printf("Core version: %s\n", ESP.getCoreVersion());

    // --- Flash ---
    Serial.printf("Flash size:  %d bytes (%.2f MB)\n", ESP.getFlashChipSize(), ESP.getFlashChipSize() / 1024.0 / 1024.0);
    Serial.printf("Flash speed: %d Hz\n", ESP.getFlashChipSpeed());

    // --- Внутрішня RAM (SRAM) ---
    Serial.printf("Total heap:  %d bytes\n", ESP.getHeapSize());
    Serial.printf("Free heap:   %d bytes\n", ESP.getFreeHeap());

    // --- PSRAM ---
    Serial.printf("PSRAM found: %s\n", psramFound() ? "YES" : "NO");
    if (psramFound()) {
        Serial.printf("Total PSRAM: %d bytes (%.2f Mb)\n", ESP.getPsramSize(), ESP.getPsramSize() / 1024.0 / 1024.0);
        Serial.printf("Free PSRAM:  %d bytes (%.2f Mb)\n", ESP.getFreePsram() / 1024.0 / 1024.0);
    }

    Serial.println();
    // Serial.printf("WiFi: %s", WiFi.SSID);
    Serial.printf("Last reset reason: %s\n", SystemReset::getLastResetReason());
    Serial.printf("display.brightness = %d\n", display.brightness());
    /* Serial.println("\n==== ESP32 HEAP INFO =====");
    heap_caps_print_heap_info(MALLOC_CAP_DEFAULT); // друкує все одразу у форматованому вигляді */
    Serial.println("============================");


    Serial.println("\n====== LittleFS INFO =======");
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
    Serial.printf("============================\n\n");
}

SerialCommandHandler commandHandler;
void setupSerialCommander() {
    commandHandler.registerCommand("status", "Показати статус пристрою", [](const String& args) {
        dumpChipsetInfo();
    });

    commandHandler.registerCommand("reboot", "Перезавантажити пристрій", [](const String& args) {
        SystemReset::reboot();
    });

    commandHandler.registerCommand("led", "Керування світлодіодом: led on|off", [](const String& args) {
        if (args.equalsIgnoreCase("on")) {
            Serial.println(F("LED увімкнено"));
        } else if (args.equalsIgnoreCase("off")) {
            Serial.println(F("LED вимкнено"));
        } else {
            Serial.println(F("Використання: led on|off"));
        }
    });

    Serial.println("SerialCommander setup done");
 }

void setupBackgroundImage() {
    #if defined(LITTLEFS_BACKGROUND_IMAGE)
    spaceImage.loadFromLittleFS(LITTLEFS_BACKGROUND_IMAGE, SPRITE_COLOR_DEPTH > 8 ? JpegColorDepth::RGB565 : JpegColorDepth::RGB332);
    setBackgroundImage(spaceImage);
    #endif
}

void setupConfigStorage() {
    configStorage.begin(PIO_PIOENV);
    Serial.println("CondfigStorage init done");
}

void loadConfig() {
    display.brightness(configStorage.getFloat("brightness", 50));
    display.autobrightness(configStorage.getFloat("auto-brightness", false));
    Serial.println("ConfigStorage load done");
}

void setupEventDispatcher() {
    dispatcher.addListener("display.brightness", [](IEvent& e) {
        // auto& ev = static_cast<SensorReadyEvent&>(e);
        // Serial.printf("Sensor value: %.2f\n", ev.value());
        Serial.printf("Event::display.brightness(%d)\n", display.brightness());
    });

    Serial.println("EventDispatcher setup done");
}

void setup() {
    setupSerial();
    setupEventDispatcher();
    setupConfigStorage();
    setupSerialCommander();
    setupLittleFS();
    setupDisplay();
    setupTouchScreen();
    setupWiFi();
    setupNtpService();
    setupBackgroundImage();
    loadConfig();

    display.flush();

    Serial.println("\n> Ready. Введіть 'list' для перегляду команд.\n");
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
  display.printf("CPU: %d MHz   Loop rate: %d/s", cpuFreq, display.loopFrameRate());

  display.setCursor(10, 10 + 2 * (5 +  display.fontHeight()));
  display.printf("Heap free: %d KB / %d KB (%d%%)", freeHeap / 1024, totalHeap / 1024, heapPercent);

  display.setCursor(10, 10 + 3 * (5 +  display.fontHeight()));
  display.print(dumpPingStatsStr());

  char brightnessStr[200];
  sprintf(brightnessStr, "Brigtness: %d%% lightsensor: %s (%d)", display.brightness(), display.hasLightSensor() ? "yes" : "no", display.lightSensor());
  display.setCursor(10, 10 + 4 * (5 +  display.fontHeight()));
  display.print(brightnessStr);

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
    commandHandler.update();
    doPing();
    // display.clear();
    drawBackgroundImage();
    drawSystemInfo();
    drawTime();

    //sendEmail();

    scheduler.loop();
    touchController.update();

    display.flush();
    //heapMonitor.update();
    delay(10);
}
