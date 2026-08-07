#pragma once

// Print-обгортка, що розсилає кожен write() у кілька підписаних Print-виходів
// (наприклад, Serial + ScreenLogTail). Джерело (SerialLogger/будь-який код,
// що пише в Print&) не знає про підписників - залежність інвертована через
// існуючу абстракцію Print. Підписників додає той, хто конструює SerialLogger
// (main.cpp / src-<board>/), сам PrintFanout нічого не знає ні про Logger,
// ні про ScreenLogTail.
//
// Fixed-size список підписників (без heap), capacity - compile-time шаблонний
// параметр. Конструктор зі списком виходів фіксує capacity рівно на кількість
// переданих аргументів (deduction guide нижче) - для додавання підписників
// після конструювання лишається add(), але понад початкову кількість вже
// не буде місця; якщо потрібен запас - вказати kMaxOutputs явно.
//
// Використання:
//   static PrintFanout fanout{Serial, screenLogTail()};  // capacity = 2
//   static SerialLogger _log{"app", fanout};
//
//   // або з запасом місця під подальші add():
//   static PrintFanout<4> fanout;
//   fanout.add(Serial);
//   fanout.add(screenLogTail());

#include <Print.h>

#include <cstddef>

template <size_t kMaxOutputs>
class PrintFanout : public Print {
public:
  PrintFanout() = default;

  template <typename... Outputs>
  explicit PrintFanout(Outputs&... outputs) : PrintFanout() {
    (add(outputs), ...);
  }

  // Повертає false, якщо ліміт підписників (kMaxOutputs) вичерпано.
  bool add(Print& output) {
    if (_count >= kMaxOutputs) {
      return false;
    }
    _outputs[_count++] = &output;
    return true;
  }

  size_t write(uint8_t c) override {
    for (size_t i = 0; i < _count; ++i) {
      _outputs[i]->write(c);
    }
    return 1;
  }

  size_t write(const uint8_t* buffer, size_t size) override {
    for (size_t i = 0; i < _count; ++i) {
      _outputs[i]->write(buffer, size);
    }
    return size;
  }

private:
  Print* _outputs[kMaxOutputs] = {};
  size_t _count = 0;
};

template <typename... Outputs>
PrintFanout(Outputs&...) -> PrintFanout<sizeof...(Outputs)>;
