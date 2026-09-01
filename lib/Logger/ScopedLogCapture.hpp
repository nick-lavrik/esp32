#pragma once

// RAII-скоуп захоплення логу: поки об'єкт живий, кожен рядок, що йде через
// SerialLogger у поточному таску, дублюється в sink (у Serial він іде як і
// раніше). У деструкторі відновлюється sink, що стояв до цього - тож скоупи
// можна вкладати.
//
// Використання:
//   CommandResponse resp(target);
//   {
//     ScopedLogCapture capture(resp);
//     commandHandler.execute(line);
//   }
//   resp.finish();   // ПІСЛЯ зняття скоупу, інакше логи самої доставки
//                    // потрапили б у власну відповідь
//
// Окремий випадок - ScopedLogCapture{nullptr}: тимчасово ЗНІМАЄ захоплення.
// Потрібно як re-entrancy guard всередині доставки відповіді.
//
// Працює лише з SerialLogger-бекендом (-D USE_SERIAL_LOGGER, стоїть у
// [common] для всіх плат); EspLogger пише в esp_log і повз цей механізм.

#include <Print.h>

#include <cstddef>

#include "LogCaptureRegistry.hpp"

class ScopedLogCapture {
public:
  explicit ScopedLogCapture(Print& sink) : _previous(LogCaptureRegistry::instance().swap(&sink)) {}

  explicit ScopedLogCapture(std::nullptr_t) : _previous(LogCaptureRegistry::instance().swap(nullptr)) {}

  ~ScopedLogCapture() { LogCaptureRegistry::instance().swap(_previous); }

  ScopedLogCapture(const ScopedLogCapture&) = delete;
  ScopedLogCapture& operator=(const ScopedLogCapture&) = delete;
  ScopedLogCapture(ScopedLogCapture&&) = delete;
  ScopedLogCapture& operator=(ScopedLogCapture&&) = delete;

private:
  Print* _previous;
};
