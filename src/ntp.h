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

  // img.fillRect(200, 5, 120, 20, BG_COLOR);
  display.setTextSize(2);
  // img.setTextColor(TEXT_MAIN, BG_COLOR);
  display.setTextColor(TFT_WHITE);

  display.setCursor(max(0, display.width() - 10 - display.textWidth(timeStr)), 8);
  display.print(timeStr);

  Serial.printf("%s\n", timeStr);
}
