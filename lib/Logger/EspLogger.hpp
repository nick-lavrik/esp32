#pragma once
#include "ILogger.hpp"

// Реалізація ILogger поверх esp_log.h (ESP-IDF).
// Фільтрація рівня йде через LogLevelManager (ієрархія тегів),
// а не через esp_log_level_set/get напряму.
class EspLogger : public ILogger {
public:
    explicit EspLogger(const char* tag);

protected:
    void log(LogLevel level, const char* fmt, va_list args) override;
};
