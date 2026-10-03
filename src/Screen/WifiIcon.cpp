#include "WifiIcon.hpp"

#include "App/AppGlobals.hpp"
#include "ScreenManager.hpp"

#if defined(ESP8266)
#include <ESP8266WiFi.h>
#else
#include <WiFi.h>
#endif

// SSD1306 (esp8266) монохромний: "кольорів" TFT_* у нього немає.
#if defined(ESP8266)
#define TFT_WHITE WHITE
#define TFT_GREEN WHITE
#define TFT_DARKGREY WHITE
#define TFT_DARKGREEN WHITE
#endif
#include <MonoIcon16x16.hpp>
MonoIcon16x16 icon;
void setupWiFiIcon() {
#if defined(BOARD_ESP8266)
  const int p[2] = {display.width() - 16, display.height() - 16};
#elif defined(BOARD_ESP32_S3_LCD147)
  const int p[2] = {display.width() - 16, display.height() - 16};
#elif defined(BOARD_ESP32_C6) || defined(BOARD_ESP32_C6_LCD096)
  const int p[2] = {display.width() - 16, display.height() - 16};
#elif defined(BOARD_4848S040)
  const int p[2] = {display.width() - 16, display.height() - 16};
#elif defined(BOARD_ST7789)
  const int p[2] = {display.width() - 16, display.height() - 16};
#else
  const int p[2] = {display.width() - 16, 0};
#endif

  // Logger::info("================ Display %dx%d", display.width(), display.height());

  scheduler.addCronTask(0, [p]() {
    // scheduler.loop() крутиться ВСЕРЕДИНІ транзакції кадру, тому цей таск
    // домалював би іконку поверх будь-якого екрана - малюємо лише там, де
    // накладки дозволені (Screen::overlays()).
    if (!screens.active().overlays()) return;
    /* display.drawRect(0, 0, 2, 2, TFT_GREEN);
    display.drawRect(10, 10, 2, 2, TFT_GREEN);
    display.drawRect(20, 20, 2, 2, TFT_GREEN);
    display.drawRect(display.width()-2, 0, 2, 2, TFT_GREEN);

    display.drawRect(TFT_WIDTH - 16, TFT_HEIGHT - 16, 2, 2, TFT_WHITE);
    display.drawRect(TFT_WIDTH - 20, TFT_HEIGHT - 20, 12, 12, TFT_GREEN); */

    if (WiFi.isConnected()) {
      display.drawBitmap(p[0], p[1], (const uint8_t*)icon.wifi().data(), 16, 16, TFT_DARKGREEN);
    } else {
      display.drawBitmap(p[0], p[1],
                         (const uint8_t*)((uint)(millis() % 1000) >= 450 ? icon.wifi().data() : icon.empty().data()),
                         16, 16, TFT_DARKGREY);
    }
  });
}
