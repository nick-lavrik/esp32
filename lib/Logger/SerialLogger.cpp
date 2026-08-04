#include "SerialLogger.hpp"
#include "LogLevelManager.hpp"
#include <cstdio>
#include <RwLock.hpp>

SerialLogger::SerialLogger(const char* tag, Print& output)
    : ILogger(tag), _output(output) {}

const char* SerialLogger::levelName(LogLevel level) {
    switch (level) {
        case LogLevel::Error:   return "E";
        case LogLevel::Warn:    return "W";
        case LogLevel::Info:    return "I";
        case LogLevel::Debug:   return "D";
        case LogLevel::Verbose: return "V";
        default:                return "?";
    }
}

void SerialLogger::log(LogLevel level, const char* fmt, va_list args) {
    if (level > LogLevelManager::instance().getLevel(_tag)) {
        return;
    }

    static constexpr size_t BUF_SIZE = 160;
    char buf[BUF_SIZE];

    int len = vsnprintf(buf, sizeof(buf), fmt, args);

    if (len >= static_cast<int>(sizeof(buf))) {
        // рядок обрізано vsnprintf - позначаємо явно
        buf[sizeof(buf) - 4] = '.';
        buf[sizeof(buf) - 3] = '.';
        buf[sizeof(buf) - 2] = '.';
        buf[sizeof(buf) - 1] = '\0';
    }

    if (!rwlock::write(_output, 10, [this, level, buf]() { 
            // TODO: output queue (cache)
            _output.printf("[%s][%-5s] %s\n", levelName(level), _tag, buf); 
        }))
    {
        // TODO: push <buf> into queue (cache)
    };
}
