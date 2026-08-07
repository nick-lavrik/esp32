#pragma once

// Кільцевий буфер останніх SCREEN_LOG_TAIL_LINES рядків логів - для виводу
// хвоста Serial-логу на дисплей. Реалізує Print, підписується на логер через
// PrintFanout (SerialLogger.cpp сам додає інстанс як другий вихід, якщо
// SCREEN_LOG_TAIL_LINES > 0 - див. коментар у SerialLogger.cpp). Рендер на
// екран - окрема задача, цей клас лише зберігає дані (count()/line()).
//
// Capacity - виключно compile-time (build_flags), в рантаймі не змінюється.
// Задається через build_flags, напр.:
//   build_flags = -D SCREEN_LOG_TAIL_LINES=20
// SCREEN_LOG_TAIL_LINES == 0 (за замовчуванням, якщо не задано) - клас
// повністю вимкнений: буфер не виділяється (kCapacity == 0 -> _lines[0][...],
// нульовий розмір), write()/push() - no-op.
//
// Рядки довші за kLineSize - 1 символ обрізаються (truncate), без overflow
// у нову строку - навмисне спрощення, не помилка.
//
// Пауза (pause()/resume()) - єдиний підтримуваний рантайм-режим: напр. щоб
// не засмічувати буфер під час показу самого лог-екрана touch-жестом.
//
// Використання:
//   static ScreenLogTail _tail;             // buffer з SCREEN_LOG_TAIL_LINES рядків
//   for (size_t i = 0; i < _tail.count(); ++i) {
//     tft.println(_tail.line(i));           // від найстарішого до найновішого
//   }
//   _tail.pause();  / _tail.resume();

#include <Print.h>

#include <cstddef>
#include <cstdint>

#ifndef SCREEN_LOG_TAIL_LINES
#define SCREEN_LOG_TAIL_LINES 0
#endif

class ScreenLogTail : public Print {
public:
  static constexpr size_t kCapacity = SCREEN_LOG_TAIL_LINES;
  static constexpr size_t kLineSize = 160;

  ScreenLogTail() = default;

  size_t write(uint8_t c) override;

  // Кількість рядків, що фактично накопичені (0..kCapacity).
  size_t count() const {
#if SCREEN_LOG_TAIL_LINES > 0
    return _count;
#else
    return 0;
#endif
  }

  // indexFromOldest: 0 - найстаріший накопичений рядок, count()-1 - найновіший.
  const char* line(size_t indexFromOldest) const;

  void clear();

  void pause() { _paused = true; }
  void resume() { _paused = false; }
  bool isPaused() const { return _paused; }

private:
#if SCREEN_LOG_TAIL_LINES > 0
  void pushCurrentLine();

  char _lines[kCapacity][kLineSize] = {};
  size_t _head = 0;   // індекс найстарішого рядка
  size_t _count = 0;  // скільки рядків фактично накопичено

  char _current[kLineSize] = {};
  size_t _currentLen = 0;
#endif

  bool _paused = false;
};

// Спільний (process-wide) хвіст логів, з яким SerialLogger.cpp з'єднує свій
// PrintFanout за замовчуванням (SCREEN_LOG_TAIL_LINES > 0). Рендер на екран
// звертається сюди напряму - SerialLogger.hpp про цю функцію/тип не знає.
ScreenLogTail& screenLogTail();
