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

    void error(const char* fmt, ...) {
        va_list args;
        va_start(args, fmt);
        log(LogLevel::Error, fmt, args);
        va_end(args);
    }

    void warn(const char* fmt, ...) {
        va_list args;
        va_start(args, fmt);
        log(LogLevel::Warn, fmt, args);
        va_end(args);
    }

    void info(const char* fmt, ...) {
        va_list args;
        va_start(args, fmt);
        log(LogLevel::Info, fmt, args);
        va_end(args);
    }

    void debug(const char* fmt, ...) {
        va_list args;
        va_start(args, fmt);
        log(LogLevel::Debug, fmt, args);
        va_end(args);
    }

    void verbose(const char* fmt, ...) {
        va_list args;
        va_start(args, fmt);
        log(LogLevel::Verbose, fmt, args);
        va_end(args);
    }

protected:
    virtual void log(LogLevel level, const char* fmt, va_list args) = 0;

    const char* _tag;
};
