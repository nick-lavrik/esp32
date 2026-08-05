#include "CronTask.h"

#include <Arduino.h>  // millis()

#include <utility>

CronTask::CronTask(uint32_t intervalMs, TaskCallback callback)
    : _intervalMs(intervalMs), _lastRun(millis()), _callback(std::move(callback)) {}

bool CronTask::update(uint32_t now) {
  if (now - _lastRun >= _intervalMs) {
    _lastRun = now;
    if (_callback) {
      _callback();
    }
  }
  // CronTask ніколи не повертає false сам - лише через cancel()
  // (перевіряється TaskController-ом окремо).
  return true;
}

void CronTask::onResume(uint32_t pausedForMs) {
  // Зсуваємо мітку останнього запуску, щоб час на паузі не
  // враховувався як "минулий" інтервал.
  _lastRun += pausedForMs;
}
