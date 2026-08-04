#include "EspLogger.hpp"
#include "LogLevelManager.hpp"
#include <esp_log.h>

EspLogger::EspLogger(const char* tag) : ILogger(tag) {}

void EspLogger::log(LogLevel level, const char* fmt, va_list args) const {
    if (level > LogLevelManager::instance().getLevel(_tag)) {
        return;
    }

    esp_log_level_t espLevel;
    switch (level) {
        case LogLevel::Error:   espLevel = ESP_LOG_ERROR;   break;
        case LogLevel::Warn:    espLevel = ESP_LOG_WARN;    break;
        case LogLevel::Info:    espLevel = ESP_LOG_INFO;    break;
        case LogLevel::Debug:   espLevel = ESP_LOG_DEBUG;   break;
        case LogLevel::Verbose: espLevel = ESP_LOG_VERBOSE; break;
        default:                espLevel = ESP_LOG_NONE;    break;
    }

    esp_log_writev(espLevel, _tag, fmt, args);
}
