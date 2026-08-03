#pragma once

#include <Arduino.h>
#include <functional>
#include <sys/time.h>
#include <vector>

// Callback викликається після кожної успішної синхронізації часу.
// tv завжди валідний: на ESP32 приходить напряму з esp_sntp,
// на ESP8266 (де рідний settimeofday_cb() параметра не має) формується
// вручну з time(nullptr) перед викликом.
using NtpSyncCallback = std::function<void(struct timeval* tv)>;

// Opaque handle для деreєстрації конкретного callback-а.
using NtpCallbackHandle = uint32_t;

static constexpr NtpCallbackHandle kInvalidNtpCallbackHandle = 0;

class NtpService {
public:
    NtpService() = default;

    // Ініціалізує SNTP (configTime) та підписується на нотифікацію синхронізації.
    // Підтримується до 3 серверів - SNTP-клієнт сам перемикається на наступний,
    // якщо попередній не відповідає (failover без додаткової логіки).
    // syncIntervalMs - лише для ESP32 (ESP-IDF), для ESP8266 інтервал
    // задається окремо через weak-функцію (мінімум 15000 мс за RFC).
    //
    // Використовує ручний offset (без урахування DST) - для точної таймзони
    // з автоматичним переходом літо/зима використовуйте beginTz().
    void begin(long gmtOffsetSec,
               int daylightOffsetSec,
               const char* ntpServer1,
               const char* ntpServer2 = nullptr,
               const char* ntpServer3 = nullptr,
               uint32_t syncIntervalMs = 60000);

    // Ініціалізує SNTP з таймзоною через POSIX TZ-рядок (напр. "EET-2EEST,M3.5.0/3,M10.5.0/4").
    // Рекомендований спосіб - libc сам рахує перехід на літній/зимовий час,
    // на відміну від begin() з фіксованим offset-ом.
    void beginTz(const char* tzString,
                 const char* ntpServer1,
                 const char* ntpServer2 = nullptr,
                 const char* ntpServer3 = nullptr,
                 uint32_t syncIntervalMs = 60000);

    // Змінює таймзону "на льоту" (без пересинхронізації NTP - це не потрібно,
    // TZ впливає лише на конвертацію localtime_r(), а не на сам SNTP-процес).
    // Зручно, якщо користувач змінює регіон у налаштуваннях вже після begin()/beginTz().
    static void setTimeZone(const char* tzString);

    // Реєструє новий callback, повертає handle для подальшого removeCallback().
    NtpCallbackHandle addCallback(NtpSyncCallback callback);

    // Видаляє callback за handle. Повертає false, якщо handle не знайдено.
    bool removeCallback(NtpCallbackHandle handle);

    // true, якщо системний час вже було синхронізовано хоча б раз.
    bool isSynced() const { return _synced; }

    // Форматує реальний (wall-clock) час з struct timeval - результат буде
    // локальним (враховує TZ, встановлений через begin()/beginTz()/setTimeZone()).
    // Розширює стандартний strftime() двома плейсхолдерами:
    //   %Q - мілісекунди (3 цифри, 000-999)
    //   %q - мікросекунди (6 цифр, 000000-999999)
    // Не використовує heap/String, лише стек - безпечно викликати з будь-якого
    // FreeRTOS-таску. buffer/format можуть співпадати з локальним стек-масивом
    // викликача. Повертає buffer (для зручного inline-використання).
    static const char* ftime(const char* format, char* buffer, size_t max,
                            const struct timeval* tv = nullptr); // nullptr -> gettimeofday() всередині

    // Форматує "аптайм" (час від старту пристрою, за замовчуванням millis()).
    // Ті самі %Q/%q плейсхолдери підтримуються.
    static const char* ftime(const char* format, char* buffer, size_t max,
                            uint64_t uptimeMs); // без default - аптайм лише явно, напр. ftime(fmt, buf, max, millis())

private:
    struct CallbackEntry {
        NtpCallbackHandle handle;
        NtpSyncCallback callback;
    };

#if defined(ESP8266)
    // ESP8266 settimeofday_cb() не передає параметр - trampoline без аргументів,
    // timeval формується всередині _onTimeSync().
    static void _onTimeSyncTrampoline();
#else
    static void _onTimeSyncTrampoline(struct timeval* tv);
#endif

    void _onTimeSync(struct timeval* tv);
    void _notifyAll(struct timeval* tv);

    void _subscribeSyncCallback(uint32_t syncIntervalMs);

    // Спільна реалізація для обох ftime()-overload-ів.
    // applyTimeZone=true -> localtime_r (реальний час), false -> gmtime_r (тривалість).
    static const char* _formatTime(const char* format, char* buffer, size_t max,
                                    time_t sec, long usec, bool applyTimeZone);

    std::vector<CallbackEntry> _callbacks;
    NtpCallbackHandle _nextHandle = 1;
    bool _synced = false;

    // Singleton-вказівник потрібен, бо C-callback API (esp_sntp / settimeofday_cb)
    // не підтримує передачу user-контексту (this).
    static NtpService* _instance;
};

/*
=============================================================================
 Приклад використання NtpService
=============================================================================

#include "NtpService.hpp"

NtpService ntp;

void setup() {
    Serial.begin(115200);
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) delay(100);

    // Callback викликається при кожній успішній синхронізації.
    ntp.addCallback([](struct timeval* tv) {
        char buf[40];
        Serial.printf("[NTP] Синхронізовано: %s\n",
                      NtpService::ftime("%Y-%m-%d %H:%M:%S.%Q", buf, sizeof(buf), *tv));
    });

    // --- Варіант 1: POSIX TZ-рядок (рекомендовано, DST рахується автоматично) ---
    ntp.beginTz("EET-2EEST,M3.5.0/3,M10.5.0/4",   // Europe/Kyiv
                "pool.ntp.org", "ua.pool.ntp.org", "time.cloudflare.com");

    // --- Варіант 2: ручний offset (без DST) ---
    // ntp.begin(2 * 3600, 0, "pool.ntp.org", "ua.pool.ntp.org");
}

void loop() {
    if (ntp.isSynced()) {
        struct timeval tv;
        gettimeofday(&tv, nullptr);
        char buf[40];
        Serial.println(NtpService::ftime("%H:%M:%S.%q", buf, sizeof(buf), tv));
    }

    // Аптайм (з мікросекундною роздільністю до 000, бо джерело - millis()):
    char up[32];
    Serial.println(NtpService::ftime("%H:%M:%S.%Q", up, sizeof(up))); // час за замовч. - millis()
}

=============================================================================
*/
