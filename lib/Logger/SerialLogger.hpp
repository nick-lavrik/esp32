#pragma once
// #include <Print.h>
#include <Arduino.h>

#include "ILogger.hpp"

// Print-вихід за замовчуванням для SerialLogger-інстансів без явного output.
// Функція (не глобальна змінна!) - навмисно: SerialCommander та інші класи
// з non-static полем `const TLogger _logger{"tag"}` конструюються як частина
// глобальних об'єктів (напр. `SerialCommander commandHandler;` у main.cpp),
// тобто ще ПІД ЧАС C++ static-init, до setup(). Порядок ініціалізації
// глобальних змінних між різними translation units не гарантований - якби
// serialLoggerOutput була звичайною extern-змінною (означеною через виклик
// функції в SerialLogger.cpp), той виклик міг ще не встигнути виконатись
// на момент конструювання SerialCommander в main.cpp (undefined behavior:
// посилання на ще не існуючий об'єкт) - static initialization order fiasco.
// Функція з лінивою static-локальною змінною (Meyer's singleton) гарантовано
// ініціалізується при першому виклику, незалежно від порядку TU - тому
// безпечна як default-аргумент нижче.
// Визначення - в SerialLogger.cpp: якщо SCREEN_LOG_TAIL_LINES > 0, це
// PrintFanout<2>(Serial + ScreenLogTail-хвіст), інакше - просто Serial.
// SerialLogger.hpp навмисно не include-ить PrintFanout.hpp/ScreenLogTail.hpp -
// лише декларація функції, що повертає Print&, обране SerialLogger.cpp.
Print& serialLoggerOutput();

// Реалізація ILogger поверх Print (Serial за замовчуванням),
// працює однаково на ESP32 і ESP8266 Arduino core.
// Фільтрація рівня йде через LogLevelManager (ієрархія тегів).
class SerialLogger : public ILogger {
public:
  explicit SerialLogger(const char* tag, Print& output = serialLoggerOutput());

protected:
  void log(LogLevel level, const char* fmt, va_list args) const override;

private:
  static const char* levelName(LogLevel level);

  Print& _output;
};
