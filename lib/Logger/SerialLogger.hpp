#pragma once

#include <Arduino.h>

#include "ILogger.hpp"

// Реалізація ILogger, що ПУБЛІКУЄ записи в Journal.
//
// Друком у Serial цей клас більше не займається - це робить SerialSink
// (lib/Journal), один із рівноправних приймачів журналу. Через це зник і
// параметр `Print& output`, і функція serialLoggerOutput(): вихід у логера
// більше не один і не його справа.
//
// Що лишилось за логером: виклик фільтра рівня (самі правила живуть у журналі,
// поруч із матчингом тегів) і форматування ТЕКСТУ повідомлення - обрізання по межі
// UTF-8 з маркером "...". Префікс "[I][tag    ] " додає приймач.
class SerialLogger : public ILogger {
public:
  explicit SerialLogger(const char* tag);

protected:
  void log(LogLevel level, const char* fmt, va_list args) const override;
};
