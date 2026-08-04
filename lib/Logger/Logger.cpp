#include "Logger.hpp"

#include <cstdarg>

#include "TLogger.hpp"

namespace {

const TLogger& backend() {
  static TLogger instance(LOGGER_DEFAULT_TAG);
  return instance;
}

}  // namespace

void Logger::error(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  backend().logv(LogLevel::Error, fmt, args);
  va_end(args);
}

void Logger::warn(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  backend().logv(LogLevel::Warn, fmt, args);
  va_end(args);
}

void Logger::info(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  backend().logv(LogLevel::Info, fmt, args);
  va_end(args);
}

void Logger::debug(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  backend().logv(LogLevel::Debug, fmt, args);
  va_end(args);
}

void Logger::verbose(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  backend().logv(LogLevel::Verbose, fmt, args);
  va_end(args);
}
