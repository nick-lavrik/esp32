#pragma once
#include <cstdarg>
#include <Print.h>

enum class LogLevel { Error = 0, Warn = 1, Info = 2, Debug = 3, Verbose = 4 };

class ILogger : public Print {
public:
  explicit ILogger(const char* tag) : _tag(tag) {}
  virtual ~ILogger() = default;

  static const size_t BUFFER_SIZE = 128; // Размер буфера в байтах
  char _buffer[BUFFER_SIZE];
  size_t _index = 0;

  // Внутренний метод для физической отправки накопленных данных
  void _sendBuffer() {
    if (_index > 0) {
      debug(_buffer); // Отправляем весь буфер за раз
      _index = 0;     // Сбрасываем указатель
    }
  }

  size_t write(uint8_t c) override {
    // Если встретили перевод строки
    if (c == '\n') {
      _buffer[_index++] = (char)0;
      _sendBuffer();
      return 1;
    }

    // ... иначе сохраняем символ в буфер
    _buffer[_index++] = (char)c;

    // ... если буфер полностью заполнен
    if (_index >= BUFFER_SIZE) {
      _sendBuffer();
    }
    
    return 1; // Возвращаем 1 успешный записанный байт
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
};
