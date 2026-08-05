#include "SerialLogger.hpp"

#include <cstdio>

#include "LogLevelManager.hpp"
#include "PrintQueueRegistry.hpp"

SerialLogger::SerialLogger(const char* tag, Print& output) : ILogger(tag), _output(output) {}

const char* SerialLogger::levelName(LogLevel level) {
  switch (level) {
    case LogLevel::Error:
      return "E";
    case LogLevel::Warn:
      return "W";
    case LogLevel::Info:
      return "I";
    case LogLevel::Debug:
      return "D";
    case LogLevel::Verbose:
      return "V";
    default:
      return "?";
  }
}

void SerialLogger::log(LogLevel level, const char* fmt, va_list args) const {
  if (level > LogLevelManager::instance().getLevel(_tag)) {
    return;
  }

  static constexpr size_t BUF_SIZE = PrintQueue::kLineSize;
  char buf[BUF_SIZE];

  int len = vsnprintf(buf, sizeof(buf), fmt, args);
  if (len >= static_cast<int>(sizeof(buf))) {
    // рядок обрізано vsnprintf - позначаємо явно
    buf[sizeof(buf) - 4] = '.';
    buf[sizeof(buf) - 3] = '.';
    buf[sizeof(buf) - 2] = '.';
    buf[sizeof(buf) - 1] = '\0';
  }

  char line[BUF_SIZE];
  int lineLen = snprintf(line, sizeof(line), "[%s][%-5s] %s\n", levelName(level), _tag, buf);
  if (lineLen >= static_cast<int>(sizeof(line))) {
    line[sizeof(line) - 2] = '\n';
    line[sizeof(line) - 1] = '\0';
  }

  // Пряме write з таймаутом 10мс; якщо _output зайнятий - рядок піде
  // в per-output чергу (PrintQueueRegistry) і буде відправлений пізніше
  // наступним log()-викликом або періодичним PrintQueue::flush().
  PrintQueueRegistry::instance().forOutput(_output).tryWrite(line, /*timeoutMs=*/10);
}
