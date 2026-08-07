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
    Logger::info("NTP sync: %s", ntp.ftime("%Y-%m-%d %H:%M:%S.%q", buf, sizeof(buf), tv));
  });

  // --- Варіант 1: POSIX TZ-рядок (рекомендовано, DST рахується автоматично) ---
  // Список готових рядків для будь-якого міста:
  //   https://github.com/nayarsystems/posix_tz_db/blob/master/zones.csv
  //
  // Специфікація формату TZ:
  //   https://www.gnu.org/software/libc/manual/html_node/TZ-Variable.html

  ntp.beginTz("EET-2EEST,M3.5.0/3,M10.5.0/4",  // Europe/Kyiv
              "pool.ntp.org", "ua.pool.ntp.org", "time.cloudflare.com",
              6000000);  // 600 sec - (default: 3_600_000)
}
