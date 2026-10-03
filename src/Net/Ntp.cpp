#include "Ntp.hpp"

#include <Logger.hpp>
#include <NtpService.hpp>

#include "App/AppGlobals.hpp"

void setupNtpService() {
  // Callback викликається при кожній успішній синхронізації.
  ntp.addCallback([](struct timeval* tv) {
    char buf[80] = "";
    Logger::info("NTP sync: %s", ntp.ftime("%Y-%m-%d %H:%M:%S.%q", buf, sizeof(buf), tv));
  });

  // POSIX TZ-рядок: DST рахується автоматично.
  // Список готових рядків для будь-якого міста:
  //   https://github.com/nayarsystems/posix_tz_db/blob/master/zones.csv
  //
  // Специфікація формату TZ:
  //   https://www.gnu.org/software/libc/manual/html_node/TZ-Variable.html
  ntp.beginTz("EET-2EEST,M3.5.0/3,M10.5.0/4",  // Europe/Kyiv
              "pool.ntp.org", "ua.pool.ntp.org", "time.cloudflare.com",
              6000000);  // 6000 с = 100 хв (дефолт NtpService - 60000, тобто 1 хв)
}
