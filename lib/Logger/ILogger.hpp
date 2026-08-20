#pragma once
#include <cstdarg>
#include <Print.h>

enum class LogLevel { Error = 0, Warn = 1, Info = 2, Debug = 3, Verbose = 4 };

class ILogger : public Print {
public:
  explicit ILogger(const char* tag) : _tag(tag) {}
  virtual ~ILogger() = default;

  static const size_t BUFFER_SIZE = 128;  // розмір буфера в байтах (з '\0')

  // Print-інтерфейс: збирає символи до '\n' і віддає готовий рядок у log().
  // Потрібен консументам, що пишуть у Print& (напр.
  // EspPartitionInspector::printAll(Print&), SDCardInspector::printAll(..., Print&)),
  // яким передають TLogger як вихід.
  size_t write(uint8_t c) override {
    if (c == '\n') {
      _sendBuffer();
      return 1;
    }

    // Рядок довший за буфер віддаємо частинами: місце під '\0' резервуємо
    // ЗАВЖДИ (_index <= BUFFER_SIZE - 1), інакше _sendBuffer() читав би за
    // межі масиву - у vsnprintf() потрапляв би не термінований буфер.
    _buffer[_index++] = (char)c;
    if (_index >= BUFFER_SIZE - 1) {
      _sendBuffer();
    }

    return 1;  // Print очікує кількість "записаних" байтів
  }

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
  void logv(LogLevel level, const char* fmt, va_list args) const { log(level, fmt, args); }

protected:
  LogLevel _defaultLogLevel = LogLevel::Debug;

  virtual void log(LogLevel level, const char* fmt, va_list args) const = 0;
  const char* _tag;

private:
  // Віддає накопичений рядок у лог і скидає буфер.
  //
  // ВАЖЛИВО: "%s", _buffer - а НЕ debug(_buffer). Накопичений текст приходить
  // від довільного консумента Print& (таблиця розділів, вміст SD, тощо) і
  // цілком може містити '%'. Як format-рядок він змусив би vsnprintf() читати
  // неіснуючі varargs зі стека.
  void _sendBuffer() {
    _buffer[_index] = '\0';  // місце завжди зарезервоване, див. write()
    // Без "if (_index > 0)": порожній println() має давати порожній рядок у
    // лозі, як і раніше (роздільники в таблицях EspPartitionInspector тощо).
    debug("%s", _buffer);
    _index = 0;
  }

  // Рівень навмисно Debug (як і було): вивід через Print& - діагностичний.
  // Наслідок: при DEFAULT_LOG_LEVEL < 3 вивід "status flash"/"status sd"
  // буде відфільтрований разом зі звичайними debug-повідомленнями.
  char _buffer[BUFFER_SIZE] = {};
  size_t _index = 0;
};
