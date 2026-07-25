#include "JobTask.h"

#include <Arduino.h> // millis()
#include <utility>

JobTask::JobTask(uint32_t durationMs, TaskCallback callback, uint32_t intervalMs)
    : _startTime(millis()),
      _durationMs(durationMs),
      _intervalMs(intervalMs),
      _lastCallTime(0),
      _callback(std::move(callback)) {
}

bool JobTask::update(uint32_t now) {
    if (now - _startTime >= _durationMs) {
        return false; // час вийшов - видалити з черги
    }

    if (_intervalMs == 0 || (now - _lastCallTime) >= _intervalMs) {
        if (_callback) {
            _callback();
        }
        _lastCallTime = now;
    }

    return true; // ще активне
}
