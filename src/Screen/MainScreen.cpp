#include "MainScreen.hpp"

#include <Logger.hpp>

#if defined(BOARD_ESP8266)
#include <ESP8266WiFi.h>
#else
#include <WiFi.h>
#endif
#include <NetworkSupervisor.hpp>

#include "App/AppGlobals.hpp"
#include "BackgroundImages.hpp"
#include "ScreenControl.hpp"
#include "ScreenManager.hpp"
#include "Net/Ping.hpp"

namespace {

void drawSystemInfo() {
  char buf[120] = "";
  uint8_t row = 0;
  #if BOARD_ESP32_C6_LCD096
  constexpr uint8_t space = 2;
  constexpr uint8_t top = 2; // space
  constexpr uint8_t left = 2; // space
  #else
  constexpr uint8_t space = 5;
  constexpr uint8_t top = 10; // space * 2
  constexpr uint8_t left = 10; // space * 2
  #endif
  // img.fillRect(0, 30, 320, 65, BG_COLOR);

  uint32_t freeHeap = ESP.getFreeHeap();
  uint32_t cpuFreq = ESP.getCpuFreqMHz();
  uint32_t uptimeSec = millis() / 1000;

  display.setTextSize(1);
  display.setTextFont(1);
  display.setTextColor(TFT_DARKGREY);

#if defined(ESP32)
  display.setCursor(left, top + row++ * (space + display.fontHeight()));
  // %u + (unsigned): uint32_t на RISC-V (C6) це "long unsigned int", тобто під
  // %d він не підходить (varargs типи мусять збігатися). Каст робить рядок
  // однаковим і для Xtensa, і для RISC-V.
  display.printf(F("Uptime: %02u:%02u:%02u"), (unsigned)(uptimeSec / 3600),
                 (unsigned)((uptimeSec / 60) % 60), (unsigned)(uptimeSec % 60));
#endif

#if defined(ESP8266)
  display.setCursor(0, 1 + row++ * (2 + display.fontHeight()));
  snprintf(buf, sizeof(buf), "CPU: %dMHz\nLoop rate: %d/s", cpuFreq, display.loopFrameRate());
  display.print(buf);

  enum ScreenMode { DISPLAY_INFO, NETWORK, UPTIME };
  static ScreenMode currentScreen = NETWORK;
  static uint32_t currentScreenTs = millis();
  const uint32_t screenDelayMs = 3 * 1000UL;
  uint32_t hfree; uint32_t hmax; uint8_t hfrag;
  
  switch (currentScreen) {
    case DISPLAY_INFO:
      snprintf(buf, sizeof(buf), "Display: %dx%d\nBrightness: %d\n", TFT_WIDTH, TFT_HEIGHT, display.brightness());
      break;
    case NETWORK:
      snprintf(buf, sizeof(buf), "WiFi: %s\nIP:   %s", WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
      break;
    case UPTIME:
      ESP.getHeapStats(&hfree, &hmax, &hfrag);
      snprintf(buf, sizeof(buf), "Heap: %d / %d KB\nUptime: %02d:%02d:%02d", 
          hmax / 1024, hfree / 1024,
          uptimeSec / 3600, (uptimeSec / 60) % 60, uptimeSec % 60);
      break;
  }

  display.setCursor(0, TFT_HEIGHT - 2 * (0 + display.fontHeight()));
  display.print(buf);

  if (millis() - currentScreenTs > screenDelayMs) {
    currentScreen = (ScreenMode)((currentScreen + 1) % 3);
    currentScreenTs = millis();
  }

#elif BOARD_ESP32_C6_LCD096
  display.setCursor(left, top + row++ * (space + display.fontHeight()));
  snprintf(buf, sizeof(buf), "CPU: %u MHz", (unsigned)cpuFreq);
  display.print(buf);

  display.setCursor(left, top + row++ * (space + display.fontHeight()));
  snprintf(buf, sizeof(buf), "Loop rate: %u/s", (unsigned)display.loopFrameRate());
  display.print(buf);
#else
  display.setCursor(left, top + row++ * (space + display.fontHeight()));
  snprintf(buf, sizeof(buf), "CPU: %u MHz   Loop rate: %u/s", (unsigned)cpuFreq,
           (unsigned)display.loopFrameRate());
  display.print(buf);
#endif


#if defined(ESP8266)
  // ESP8266 не має ESP.getHeapSize() - показуємо лише вільну пам'ять
  // display.setCursor(10, 10 + row++ * (5 + display.fontHeight()));
  // display.printf("Heap free: %d KB", freeHeap / 1024);
#else
  uint32_t totalHeap = ESP.getHeapSize();
  display.setCursor(left, top + row++ * (space + display.fontHeight()));
  // %u + (unsigned) - див. коментар біля "Uptime" вище.
  // Порядок множення теж важливий: freeHeap * 100 для ~320 KB купи ще влазить
  // у 32 біти, але запас невеликий - рахуємо через 64-бітний проміжок.
  display.printf("Heap free: %u / %u (%u%%)", (unsigned)(freeHeap / 1024),
                 (unsigned)(totalHeap / 1024),
                 (unsigned)(totalHeap ? (uint64_t)freeHeap * 100 / totalHeap : 0));
#endif

#if defined(ESP32)
  char* dumpPingStr = dumpPingStatsStr();
  if (dumpPingStr) {
    display.setCursor(left, top + row++ * (space + display.fontHeight()));
    display.print(dumpPingStr);  // було: повторний виклик dumpPingStatsStr()
  }

  #if BOARD_ESP32_C6_LCD096
  // Вузький екран (160px) - без відсотків, тому й wifiSignalQuality() тут не
  // рахуємо (раніше передавався третім, зайвим аргументом на два %-специфікатори).
  snprintf(buf, sizeof(buf), "WiFi: %s (%d dBm)", WiFi.SSID().c_str(), WiFi.RSSI());
  #else
  snprintf(buf, sizeof(buf), "WiFi: %s (%d dBm / %d%%)", WiFi.SSID().c_str(), WiFi.RSSI(), wifiSignalQuality(WiFi.RSSI()));
  #endif
  display.setCursor(left, top + row++ * (space + display.fontHeight()));
  display.print(buf);


  snprintf(buf, sizeof(buf), "IP: %s", WiFi.localIP().toString().c_str());
  display.setCursor(left, top + row++ * (space + display.fontHeight()));
  display.print(buf);

  snprintf(buf, sizeof(buf), "Brightness: %d%% %s", display.brightness(), isAutoBrightness ? "(auto)" : "");
  display.setCursor(left, top + row++ * (space + display.fontHeight()));
  display.print(buf);

  #if BOARD_HAS_LIGHT_SENSOR
    // display.setTextSize(1);
    // display.setTextColor(TFT_DARKGREY);
    // display.setCursor(10, display.height() - 1 * (5 + display.fontHeight()));
    display.setCursor(left, top + row++ * (space + display.fontHeight()));
    display.printf("LightSensor: %4d (%3d%%)", lightSensor.read(), lightSensor.value());
  #endif

#endif

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

void drawTime() {
  static uint32_t lastErrorMs = 0;
  if (!ntp.isSynced()) {
    const char* msg = "TIME SYNC";
    display.setTextSize(2);
    display.setTextColor(TFT_RED);
    display.setCursor(
      max(0, (int) (display.width() - display.textWidth(msg)) / 2),
      max(0, (int) (display.height() - display.fontHeight()) / 2)
    );
    // 128x64.108
    // (128-108)/2 = 10
    // Logger::warn("Time sync failed!, pos(%d, %d, %dx%d.%d)", x, y, display.width(), display.height(), display.textWidth(msg));
    if (lastErrorMs == 0) {
      lastErrorMs = millis() - 2000; // first message in 3 sec, all other after 5 second
    }
    if (millis() - lastErrorMs > 5000) {
      Logger::warn("Time sync failed!");
      lastErrorMs = millis();
    }
    display.print(msg);
    display.setTextSize(1);
    return;
  }

  char timeStr[16];
  ntp.ftime("%H:%M:%S", timeStr, sizeof(timeStr));
  // ntp.ftime("%H:%M:%S.%Q", timeStr, sizeof(timeStr));

#if CLOCK_TEXT_FONT && CLOCK_TEXT_SIZE && CLOCK_POS_Y
  display.setTextFont(CLOCK_TEXT_FONT);
  display.setTextSize(CLOCK_TEXT_SIZE);
  #if !defined(CLOCK_POS_X)
  int x = (display.width() - display.textWidth(timeStr)) / 2;
  #else
  int x = CLOCK_POS_X;
  #endif

  display.setTextColor(TFT_CYAN);
  display.setCursor(x, CLOCK_POS_Y);
  display.print(timeStr);

  #if DATE_TEXT_FONT && DATE_TEXT_SIZE && DATE_POS_Y
  display.setTextFont(DATE_TEXT_FONT);
  display.setTextSize(DATE_TEXT_SIZE);

  char dateStr[16];
  ntp.ftime("%d.%m.%Y", dateStr, sizeof(dateStr));

  #if !defined(DATE_POS_X)
  int dateX = (display.width() - display.textWidth(dateStr)) / 2;
  #else
  int dateX = DATE_POS_X;
  #endif
  display.setTextColor(TFT_ORANGE);
  display.setCursor(dateX, DATE_POS_Y);
  display.print(dateStr);

  #endif

#elif BOARD_TTGO_T1 || BOARD_ESP32_S3_LCD147
  // time
  #if BOARD_ESP32_C6
  display.setTextSize(5);
  #elif BOARD_ESP32_C6_LCD096
  display.setTextSize(2);
  #else
  display.setTextFont(7);  // великий "цифровий" шрифт (тільки цифри та ":")
  display.setTextSize(1);
  #endif

  int textW = display.textWidth(timeStr);
  int x = (display.width() - textW) / 2;
  #if BOARD_ESP32_C6
  int y = display.fontHeight();
  #else
  int y = 30;
  #endif

  // display.getTextBound();
  // Затираємо попередній текст перед виводом нового
  // display.fillRect(0, y, display.width(), display.fontHeight(), TFT_BLACK);

  // display.setTextColor(TFT_DARKGREY);
  display.setTextColor(TFT_CYAN);
  display.setCursor(x, y);
  display.print(timeStr);

  // date
  char dateStr[16];
  ntp.ftime("%d.%m.%Y", dateStr, sizeof(dateStr));

  // Logger::info("font_height (clock) = %d", display.fontHeight()); // 40 (!)
  y += display.fontHeight() + 5;

  display.setTextFont(4);
  display.setTextSize(1);

  textW = display.textWidth(dateStr);
  x = (display.width() - textW) / 2;
  // Logger::info("font_height (date ) = %d", display.fontHeight()); // 28 (?)

  // display.fillRect(0, y, display.width(), display.fontHeight(), TFT_BLACK);

  // display.setTextColor(TFT_DARKGREEN);
  display.setTextColor(TFT_ORANGE);
  display.setCursor(x, y);
  display.print(dateStr);

  display.setTextSize(1);
  display.setTextFont(1);
#elif BOARD_ESP8266
  // display.flip();
  display.setTextSize(2);
  display.setTextColor(SSD1306_WHITE);
  int16_t x1, y1;
  uint16_t textW, textH;

  // display.getTextBounds(timeStr, 0, 0, &x1, &y1, &textW, &textH);
  textW = display.textWidth(timeStr);
  int x = (TFT_WIDTH - textW) / 2;
  display.setCursor(x, 25);
  display.print(timeStr);

  // Менша дата під часом
  ntp.ftime("%d.%m.%Y", timeStr, sizeof(timeStr));

  display.setTextSize(1);
  // display.getTextBounds(dateStr, 0, 0, &x1, &y1, &textW, &textH);
  textW = display.textWidth(timeStr);
  // x = (TFT_WIDTH - textW) / 2;
  display.setCursor(TFT_WIDTH - textW, 0);
  display.print(timeStr);
#elif BOARD_4848S040 || BOARD_ST7789
  int16_t y = 8;
  uint16_t textW;
  // display.setTextFont(7); display.setTextSize(1); // великий "цифровий" шрифт (тільки цифри та ":")
  // display.setTextFont(6); display.setTextSize(1); // великий - красивий
  // display.setTextFont(4); display.setTextSize(1); // середній / так собі
  // display.setTextFont(2); display.setTextSize(2); // середній / не красиво взагалі
  display.setTextFont(1);  display.setTextSize(2); //  pretty nice
  // display.setTextColor(TFT_CYAN);
  // display.setTextColor(TFT_MAGENTA);
  display.setTextColor(TFT_DARKGREY);

  // display.getTextBounds(timeStr, 0, 0, &x1, &y1, &textW, &textH);
  textW = display.textWidth(timeStr);
  display.setCursor(display.width() - 10 - textW, y);
  display.print(timeStr);
  y += display.fontHeight();

  display.setTextFont(2);
  display.setTextSize(1);
  // display.setTextColor(TFT_ORANGE);
  ntp.ftime("%d.%m.%Y", timeStr, sizeof(timeStr));
  // display.setCursor(display.width() - 10 - textW + (textW - display.textWidth(timeStr)) / 2, y);
  display.setCursor(display.width() - 10 - display.textWidth(timeStr), y);
  display.print(timeStr);

  display.setTextFont(1);
  display.setTextSize(1);
#else
  display.setTextSize(2);
  display.setTextColor(TFT_LIGHTGREY);
  display.setCursor(max(0, display.width() - display.textWidth(timeStr) - 15), 8);
  display.print(timeStr);
#endif
}

}  // namespace

void MainScreen::drawStrip(bool frameStart) {
  (void)frameStart;
  drawBackgroundImage();
  drawSystemInfo();
  if (showClock) drawTime();
}

void MainScreen::onButton(ButtonId id, ButtonEvent event, uint32_t nowMs, uint32_t heldMs) {
  if (event == ButtonEvent::Click) {
    show_clock(!showClock);
  } else if (event == ButtonEvent::LongPress && display.brightness() == 0) {
    display_brightness(max(_savedBrightness, (uint8_t)1), _savedAutoBrightness);
  } else if (event == ButtonEvent::LongPress) {
    _savedBrightness = display.brightness();
    _savedAutoBrightness = isAutoBrightness;
    display_brightness(0, false);
  } else {
    Screen::onButton(id, event, nowMs, heldMs);
  }
}

void MainScreen::onHold(TouchPoint p, unsigned long heldMs) {
  (void)heldMs;
#if BOARD_HAS_LIGHT_SENSOR
  display_brightness(lightSensor.value(), true);
#endif
  // TODO: restore brightness before trigger autobrightness = off (!)
  // display.autobrightness(true);

  // Тип 2 (JobTask): "показувати frame"
  // постійно протягом 10 секунд, після чого само зникає з черги
  scheduler.addJob(
      10UL * 1000UL,
      [p]() {
        if (!screens.active().overlays()) return;  // інший екран - рамку не малюємо
        display.drawCircle(p.x, p.y, 4, TFT_YELLOW);
        display.drawRect(0, 0, 1, 1, TFT_WHITE);
        display.drawRect(display.width() - 1, 0, 1, 1, TFT_WHITE);
        display.drawRect(display.width() - 1, display.height() - 1, 1, 1, TFT_WHITE);
        display.drawRect(0, display.height() - 1, 1, 1, TFT_WHITE);

        display.drawRect(displayConfig.edgeZoneX, displayConfig.edgeZoneY,
                         displayConfig.screenWidth - 2 * displayConfig.edgeZoneX,
                         displayConfig.screenHeight - 2 * displayConfig.edgeZoneY, TFT_DARKGREY);
      },
      1  // з інтервалом 1 мілісекунда, а не на кожному tick()
  );

  Logger::info(" ------ !!! ONHOLD FRAME !!! ------ ");
}

void MainScreen::onSwipe(SwipeDirection dir, TouchPoint start, TouchPoint end) {
  (void)start;
  (void)end;
  // display_brightness(..., false) сам вимикає автояскравість і пише в NVS.
  if (dir == SwipeDirection::Up) {
    if (display.brightness() == 0) {
      display_brightness(1, false);
    } else if (display.brightness() == 1) {
      display_brightness(10, false);
    } else {
      display_brightness(min(100, display.brightness() + 10), false);
    }
    Logger::debug("Brightness: %d%% (increase)", display.brightness());
  } else if (dir == SwipeDirection::Down) {
    if (display.brightness() == 1) {
      display_brightness(0, false);
    } else {
      display_brightness(max(1, display.brightness() - 10), false);
    }
    Logger::debug("Brightness: %d%% (decrease)", display.brightness());
  }
}
