#include "NtpService.hpp"
#include <algorithm>
#include <stdio.h>
#include <time.h>

#if defined(ESP8266)
// ESP8266 core: інтервал синхронізації задається лише через перевизначення
// цієї weak-функції ядра (мінімум 15000 мс за RFC), сеттера немає.
extern "C" uint32_t sntp_update_delay_MS_rfc_not_less_than_15000() {
    return 60000;
}
#else
#include "esp_sntp.h"
#endif

NtpService* NtpService::_instance = nullptr;

void NtpService::begin(long gmtOffsetSec,
                        int daylightOffsetSec,
                        const char* ntpServer1,
                        const char* ntpServer2,
                        const char* ntpServer3,
                        uint32_t syncIntervalMs) {
    if (_instance != nullptr) {
        // Не критично: і Arduino configTime(), і configTzTime() самі виконують
        // sntp_stop()/sntp_init() всередині, тож повторний виклик безпечний
        // і коректно перенастроює сервери. Попереджаємо лише про те, що це
        // навмисний ре-конфіг, а не помилка ініціалізації.
        Serial.println("[NtpService] begin() викликано повторно - перенастроюю сервери");
    }
    _instance = this;

    configTime(gmtOffsetSec, daylightOffsetSec, ntpServer1, ntpServer2, ntpServer3);

    _subscribeSyncCallback(syncIntervalMs);
}

void NtpService::beginTz(const char* tzString,
                          const char* ntpServer1,
                          const char* ntpServer2,
                          const char* ntpServer3,
                          uint32_t syncIntervalMs) {
    if (_instance != nullptr) {
        Serial.println("[NtpService] beginTz() викликано повторно - перенастроюю сервери");
    }
    _instance = this;

    // configTzTime сам виконує setenv("TZ", tz, 1) + tzset() під капотом,
    // на відміну від configTime() з offset-ами.
    configTzTime(tzString, ntpServer1, ntpServer2, ntpServer3);

    _subscribeSyncCallback(syncIntervalMs);
}

void NtpService::setTimeZone(const char* tzString) {
    setenv("TZ", tzString, 1);
    tzset();
}

void NtpService::_subscribeSyncCallback(uint32_t syncIntervalMs) {
#if defined(ESP8266)
    (void)syncIntervalMs; // інтервал на ESP8266 фіксується через weak-функцію вище
    settimeofday_cb(_onTimeSyncTrampoline);
#else
    esp_sntp_set_time_sync_notification_cb(_onTimeSyncTrampoline);
    esp_sntp_set_sync_interval(syncIntervalMs);
    esp_sntp_restart();
#endif
}

NtpCallbackHandle NtpService::addCallback(NtpSyncCallback callback) {
    NtpCallbackHandle handle = _nextHandle++;
    _callbacks.push_back({handle, std::move(callback)});
    return handle;
}

bool NtpService::removeCallback(NtpCallbackHandle handle) {
    auto it = std::remove_if(
        _callbacks.begin(), _callbacks.end(),
        [handle](const CallbackEntry& entry) { return entry.handle == handle; });

    if (it == _callbacks.end()) {
        return false;
    }
    _callbacks.erase(it, _callbacks.end());
    return true;
}

#if defined(ESP8266)
void NtpService::_onTimeSyncTrampoline() {
    if (_instance == nullptr) {
        return;
    }
    // settimeofday_cb() параметра не дає, але системний час (з мікросекундами)
    // вже встановлено через settimeofday() на момент виклику цього callback-у -
    // gettimeofday() поверне ті самі tv_sec/tv_usec, які прийшли з NTP-відповіді.
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    _instance->_onTimeSync(&tv);
}
#else
void NtpService::_onTimeSyncTrampoline(struct timeval* tv) {
    if (_instance == nullptr) {
        return;
    }
    _instance->_onTimeSync(tv);
}
#endif

void NtpService::_onTimeSync(struct timeval* tv) {
    _synced = true;
    _notifyAll(tv);
}

void NtpService::_notifyAll(struct timeval* tv) {
    // Копія вектора: callback може викликати addCallback/removeCallback,
    // що інвалідує ітератори оригінального _callbacks під час проходу.
    std::vector<CallbackEntry> callbacksCopy = _callbacks;
    for (const auto& entry : callbacksCopy) {
        if (entry.callback) {
            entry.callback(tv);
        }
    }
}

const char* NtpService::_formatTime(const char* format, char* buffer, size_t max,
                                     time_t sec, long usec, bool applyTimeZone) {
    if (buffer == nullptr || max == 0) {
        return buffer;
    }
    if (format == nullptr) {
        buffer[0] = '\0';
        return buffer;
    }

    // Нормалізація usec про всяк випадок (не довіряємо викликачу).
    if (usec < 0) usec = 0;
    if (usec > 999999) usec = 999999;

    char msDigits[4]; // "000".."999" + '\0'
    char usDigits[7]; // "000000".."999999" + '\0'
    snprintf(msDigits, sizeof(msDigits), "%03ld", usec / 1000);
    snprintf(usDigits, sizeof(usDigits), "%06ld", usec);

    // Крок 1: розгортаємо власні плейсхолдери %Q (мс) і %q (мкс) у проміжний
    // формат-рядок на стеку - strftime() їх не знає і в кращому разі
    // проігнорує, а в гіршому - скопіює як є.
    constexpr size_t kMaxExpandedFormat = 160;
    char expanded[kMaxExpandedFormat];
    size_t out = 0;

    for (const char* p = format; *p != '\0' && out < kMaxExpandedFormat - 1; ++p) {
        if (p[0] == '%' && p[1] == 'Q') {
            for (const char* d = msDigits; *d != '\0' && out < kMaxExpandedFormat - 1; ++d) {
                expanded[out++] = *d;
            }
            ++p;
        } else if (p[0] == '%' && p[1] == 'q') {
            for (const char* d = usDigits; *d != '\0' && out < kMaxExpandedFormat - 1; ++d) {
                expanded[out++] = *d;
            }
            ++p;
        } else {
            expanded[out++] = *p;
        }
    }
    expanded[out] = '\0';

    // Крок 2: решту (%Y %H %M %S ...) віддаємо стандартному strftime().
    // localtime_r/gmtime_r - реентерабельні (thread-safe), на відміну від
    // localtime()/gmtime(), які використовують внутрішній static-буфер.
    struct tm ti;
    if (applyTimeZone) {
        localtime_r(&sec, &ti);
    } else {
        gmtime_r(&sec, &ti);
    }

    if (strftime(buffer, max, expanded, &ti) == 0) {
        buffer[0] = '\0'; // strftime() повертає 0 і при порожньому результаті, і при переповненні
    }
    return buffer;
}

const char* NtpService::ftime(const char* format, char* buffer, size_t max, const struct timeval* tv) {
    if (tv == nullptr) {
        struct timeval _tv;
        gettimeofday(&_tv, nullptr);
        return _formatTime(format, buffer, max, _tv.tv_sec, _tv.tv_usec,  /*applyTimeZone=*/true);
    }

    return _formatTime(format, buffer, max, tv->tv_sec, tv->tv_usec, /*applyTimeZone=*/true);
}

const char* NtpService::ftime(const char* format, char* buffer, size_t max, uint64_t uptimeMs) {
    time_t sec = static_cast<time_t>(uptimeMs / 1000);
    long usec = static_cast<long>((uptimeMs % 1000) * 1000);

    return _formatTime(format, buffer, max, sec, usec, /*applyTimeZone=*/false);
}
