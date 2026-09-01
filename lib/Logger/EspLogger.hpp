#pragma once
#include "ILogger.hpp"

// Реалізація ILogger поверх esp_log.h (ESP-IDF).
// Фільтрація рівня йде через LogLevelManager (ієрархія тегів),
// а не через esp_log_level_set/get напряму.
//
// УВАГА: ця гілка НЕ підтримує ScopedLogCapture - рядок іде напряму в
// esp_log_writev() повз LogCaptureRegistry, тож request-response для
// MQTT-команд (lib/CommandResponse) віддаватиме порожні відповіді.
// Проєкт збирається з -D USE_SERIAL_LOGGER у [common] для всіх плат, тому
// на практиці працює SerialLogger; якщо колись знадобиться саме esp_log -
// сюди треба продублювати блок capture із SerialLogger::log().
class EspLogger : public ILogger {
public:
  explicit EspLogger(const char* tag);

protected:
  void log(LogLevel level, const char* fmt, va_list args) const override;
};
