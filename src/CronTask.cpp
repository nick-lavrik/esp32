#include "CronTask.h"

#include <Arduino.h> // millis()
#include <utility>

CronTask::CronTask(uint32_t intervalMs, TaskCallback callback)
    : _intervalMs(intervalMs),
      _lastRun(millis()),
      _callback(std::move(callback)) {
}

bool CronTask::update(uint32_t now) {
    if (now - _lastRun >= _intervalMs) {
        _lastRun = now;
        if (_callback) {
            _callback();
        }
    }
    // CronTask ніколи не повертає false сам - лише через cancel()
    // (перевіряється TaskScheduler-ом окремо).
    return true;
}
