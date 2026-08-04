#pragma once

// Глобальний статичний логер - без явного тега, для швидкого логування
// у main.cpp/загальному коді, коли не потрібна ієрархія тегів.
//
// Приклад:
//   Logger::info("wifi connected, ip=%s", ip.c_str());
//   Logger::warn("mqtt reconnect attempt %d", attempt);
//   Logger::error("sd mount failed, rc=%d", rc);
//
// Тег за замовчуванням - LOGGER_DEFAULT_TAG ("app"), перевизначається через
// build_flags, напр.: -D LOGGER_DEFAULT_TAG=\"main\"
//
// Для тегованого логування з ієрархією тегів (mqtt -> mqtt.send -> ...)
// і фільтрацією рівня через LogLevelManager - використовуйте TLogger
// напряму (див. TLogger.hpp), напр.:
//   static TLogger _log{"mqtt.send"};
//   _log.info("heartbeat sent");

#ifndef LOGGER_DEFAULT_TAG
#define LOGGER_DEFAULT_TAG "app"
#endif

class Logger {
public:
  static void error(const char* fmt, ...);
  static void warn(const char* fmt, ...);
  static void info(const char* fmt, ...);
  static void debug(const char* fmt, ...);
  static void verbose(const char* fmt, ...);

  Logger() = delete;
};
