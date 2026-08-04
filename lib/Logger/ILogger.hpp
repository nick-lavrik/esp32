#pragma once
#include <cstdarg>

enum class LogLevel {
    Error = 0,
    Warn = 1,
    Info = 2,
    Debug = 3,
    Verbose = 4
};

class ILogger {
public:
    explicit ILogger(const char* tag) : _tag(tag) {}
    virtual ~ILogger() = default;

    void error(const char* fmt, ...) const {
        va_list args;
        va_start(args, fmt);
        log(LogLevel::Error, fmt, args);
        va_end(args);
    }

    void warn(const char* fmt, ...) const {
        va_list args;
        va_start(args, fmt);
        log(LogLevel::Warn, fmt, args);
        va_end(args);
    }

    void info(const char* fmt, ...) const {
        va_list args;
        va_start(args, fmt);
        log(LogLevel::Info, fmt, args);
        va_end(args);
    }

    void debug(const char* fmt, ...) const {
        va_list args;
        va_start(args, fmt);
        log(LogLevel::Debug, fmt, args);
        va_end(args);
    }

    void verbose(const char* fmt, ...) const {
        va_list args;
        va_start(args, fmt);
        log(LogLevel::Verbose, fmt, args);
        va_end(args);
    }

    // Публічний прохід до log() з готовим va_list - потрібен для фасадів
    // (напр. Logger), які самі приймають "..." і не можуть прокинути його
    // напряму (C++ не дозволяє форвардити "...", лише va_list).
    void logv(LogLevel level, const char* fmt, va_list args) const {
        log(level, fmt, args);
    }

protected:
    virtual void log(LogLevel level, const char* fmt, va_list args) const  = 0;

    const char* _tag;
};
