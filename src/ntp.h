#include <time.h>
#include "Display.h"

const char* ntpServer1 = "1.pool.ntp.org";
const char* ntpServer2 = "ua.pool.ntp.org";
const char* ntpServer3 = "pool.ntp.org";
const long  gmtOffset_sec = 2 * 3600;
const int   daylightOffset_sec = 3600;

void setupNtpService() {
    // Serial.printf("setupNtpService() %d\n", millis());
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer1, ntpServer2, ntpServer3);
    // Serial.printf("setupNtpService() %d done\n", millis());
}

extern Display display;

void drawTime() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) { 
    display.setTextSize(2);
    display.setTextColor(TFT_RED);
    display.setCursor(max(0, display.width() - 10 - display.textWidth("Time sync failed!")), 8);
    display.print("Time sync failed");
    Serial.println("Time sync failed!");
    return;
  }

  char timeStr[9];
  strftime(timeStr, sizeof(timeStr), "%H:%M:%S", &timeinfo);

  #if BOARD_TTGO_T1
  // time
    display.setTextFont(7); // великий "цифровий" шрифт (тільки цифри та ":")

    int textW = display.textWidth(timeStr);
    int textH = display.fontHeight();
    int x = (tft.width() - textW) / 2;
    int y = 30;

    // Затираємо попередній текст перед виводом нового
    // tft.fillRect(0, y, tft.width(), textH, TFT_BLACK);

    // display.setTextColor(TFT_DARKGREY);
    display.setTextColor(TFT_CYAN);
    display.setCursor(x, y);
    display.print(timeStr);

    // date
    char dateStr[16];
    strftime(dateStr, sizeof(dateStr), "%d.%m.%Y", &timeinfo);

    display.setTextFont(4);

    textW = display.textWidth(dateStr);
    x = (display.width() - textW) / 2;
    y = 100;

    // tft.fillRect(0, y, tft.width(), tft.fontHeight(), TFT_BLACK);

    // display.setTextColor(TFT_DARKGREEN);
    display.setTextColor(TFT_ORANGE);
    display.setCursor(x, y);
    display.print(dateStr);

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
    char dateStr[16];
    strftime(dateStr, sizeof(dateStr), "%d.%m.%Y", &timeinfo);

    display.setTextSize(1);
    // display.getTextBounds(dateStr, 0, 0, &x1, &y1, &textW, &textH);
    textW = display.textWidth(dateStr);
    x = (TFT_WIDTH - textW) / 2;
    display.setCursor(x, 44);
    // display.print(dateStr);
  #else
    display.setTextSize(2);
    display.setTextColor(TFT_LIGHTGREY);
    display.setCursor(max(0, display.width() - 10 - display.textWidth(timeStr)), 8);
    display.print(timeStr);
  #endif
}
