#include "NtpService.hpp"
#include <algorithm>
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
