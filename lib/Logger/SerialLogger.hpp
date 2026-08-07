#pragma once
// #include <Print.h>
#include <Arduino.h>

#include "ILogger.hpp"

// Print-вихід за замовчуванням для SerialLogger-інстансів без явного output.
// Визначення - в SerialLogger.cpp: якщо SCREEN_LOG_TAIL_LINES > 0, це
// PrintFanout<2>(Serial + ScreenLogTail-хвіст), інакше - просто Serial.
// SerialLogger.hpp навмисно не include-ить PrintFanout.hpp/ScreenLogTail.hpp -
// лише зовнішнє посилання на Print&, обране SerialLogger.cpp.
extern Print& serialLoggerOutput;

// Реалізація ILogger поверх Print (Serial за замовчуванням),
// працює однаково на ESP32 і ESP8266 Arduino core.
// Фільтрація рівня йде через LogLevelManager (ієрархія тегів).
class SerialLogger : public ILogger {
public:
  explicit SerialLogger(const char* tag, Print& output = serialLoggerOutput);

protected:
  void log(LogLevel level, const char* fmt, va_list args) const override;

private:
  static const char* levelName(LogLevel level);

  Print& _output;
};
