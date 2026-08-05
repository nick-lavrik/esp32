#include <time.h>
#if defined(ESP32)
#include "esp_sntp.h"
#endif
#include <NtpService.hpp>

#include "Display.h"
const char* ntpServer1 = "1.pool.ntp.org";
const char* ntpServer2 = "ua.pool.ntp.org";
const char* ntpServer3 = "pool.ntp.org";

const long gmtOffset_sec = 2 * 3600;
const int daylightOffset_sec = 3600;

extern NtpService ntp;
void setupNtpService() {
  // Callback викликається при кожній успішній синхронізації.
  ntp.addCallback([](struct timeval* tv) {
    // time_t now = tv->tv_sec;
    // struct tm ti;
    // localtime_r(&now, &ti);

    char buf[80] = "";
    // strftime(buf+strlen(buf), sizeof(buf)-strlen(buf), "%Y-%m-%d %H:%M:%S", &ti);
    // snprintf(buf+strlen(buf), sizeof(buf)-strlen(buf), ".%06ld", tv->tv_usec);
    // snprintf(buf+strlen(buf), sizeof(buf)-strlen(buf), " %03d-%03d",
    // static_cast<uint16_t>(tv->tv_usec / 1000), static_cast<uint16_t>(tv->tv_usec % 1000));
    // strftime(buf+strlen(buf), sizeof(buf)-strlen(buf), " %Z", &ti);
    // Serial.printf("[NTP] Синхронізовано: %s\n", buf); buf[0] = 0;
    // strftime(buf+strlen(buf), sizeof(buf)-strlen(buf), "[NTP#2] %Y-%m-%d %H:%M:%S", &ti);
    // Serial.printf("[NTP2] Синхронізовано: %s\n", buf); buf[0] = 0;
    // Serial.printf("[NTP] Синхронізовано: %s\n", ntp.ftime("%Y-%m-%d %H:%M:%S.%q", buf,
    // sizeof(buf)));  // millis() - ще не оновлено в перший раз (!)
    Logger::info("NTP Синхронізовано: %s", ntp.ftime("%Y-%m-%d %H:%M:%S.%q", buf, sizeof(buf), tv));
  });

  // --- Варіант 1: POSIX TZ-рядок (рекомендовано, DST рахується автоматично) ---
  // Список готових рядків для будь-якого міста:
  //   https://github.com/nayarsystems/posix_tz_db/blob/master/zones.csv
  //
  // Специфікація формату TZ:
  //   https://www.gnu.org/software/libc/manual/html_node/TZ-Variable.html

  ntp.beginTz("EET-2EEST,M3.5.0/3,M10.5.0/4",  // Europe/Kyiv
              "pool.ntp.org", "ua.pool.ntp.org", "time.cloudflare.com",
              600000);  // 60 sec - (default: 3_600_000)
}

extern Display display;

void drawTime() {
  struct tm timeinfo;
  if (!ntp.isSynced()) {
    display.setTextSize(2);
    display.setTextColor(TFT_RED);
    display.setCursor(max(0, display.width() - 10 - display.textWidth("Time sync failed!")), 8);
    display.print("Time sync failed");
    display.setTextSize(1);
    Logger::warn("Time sync failed!");
    return;
  }

  char timeStr[9];
  ntp.ftime("%H:%M:%S", timeStr, sizeof(timeStr));

#if BOARD_TTGO_T1
  // time
  display.setTextFont(7);  // великий "цифровий" шрифт (тільки цифри та ":")
  // display.setTextSize(1);

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
  ntp.ftime("%d.%m.%Y", dateStr, sizeof(dateStr));

  display.setTextFont(4);
  display.setTextSize(1);

  textW = display.textWidth(dateStr);
  x = (display.width() - textW) / 2;
  y = 95;

  // tft.fillRect(0, y, tft.width(), tft.fontHeight(), TFT_BLACK);

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
  char dateStr[16];
  ntp.ftime("%d.%m.%Y", dateStr, sizeof(dateStr));

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
