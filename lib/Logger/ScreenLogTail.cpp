#include "ScreenLogTail.hpp"

#include <cstring>

#if SCREEN_LOG_TAIL_LINES > 0

size_t ScreenLogTail::write(uint8_t c) {
  if (_paused) {
    return 1;
  }

  if (c == '\n') {
    pushCurrentLine();
    return 1;
  }

  // Overflow-рядки обрізаються (truncate) - навмисне спрощення, не помилка.
  if (_currentLen < kLineSize - 1) {
    _current[_currentLen++] = static_cast<char>(c);
  }

  return 1;
}

void ScreenLogTail::pushCurrentLine() {
  _current[_currentLen] = '\0';

  size_t writeIndex;
  if (_count < kCapacity) {
    writeIndex = (_head + _count) % kCapacity;
    ++_count;
  } else {
    writeIndex = _head;  // drop-oldest
    _head = (_head + 1) % kCapacity;
  }
  memcpy(_lines[writeIndex], _current, _currentLen + 1);

  _currentLen = 0;
}

const char* ScreenLogTail::line(size_t indexFromOldest) const {
  if (indexFromOldest >= _count) {
    return "";
  }
  return _lines[(_head + indexFromOldest) % kCapacity];
}

void ScreenLogTail::clear() {
  _head = 0;
  _count = 0;
  _currentLen = 0;
}

#else  // SCREEN_LOG_TAIL_LINES == 0 -> повністю вимкнено

size_t ScreenLogTail::write(uint8_t /*c*/) { return 1; }

const char* ScreenLogTail::line(size_t /*indexFromOldest*/) const { return ""; }

void ScreenLogTail::clear() {}

#endif

ScreenLogTail& screenLogTail() {
  static ScreenLogTail tail;
  return tail;
}
