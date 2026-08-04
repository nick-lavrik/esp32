#pragma once
// #include <Print.h>
#include <Arduino.h>
#include "ILogger.hpp"

// Реалізація ILogger поверх Print (Serial за замовчуванням),
// працює однаково на ESP32 і ESP8266 Arduino core.
// Фільтрація рівня йде через LogLevelManager (ієрархія тегів).
class SerialLogger : public ILogger {
public:
    explicit SerialLogger(const char* tag, Print& output = Serial);

protected:
    void log(LogLevel level, const char* fmt, va_list args) const override;

private:
    static const char* levelName(LogLevel level);

    Print& _output;
};
