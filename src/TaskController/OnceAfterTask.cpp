#include "OnceAfterTask.h"

#include <Arduino.h> // millis()
#include <utility>

OnceAfterTask::OnceAfterTask(uint32_t delayMs, TaskCallback callback)
    : _startTime(millis()),
      _delayMs(delayMs),
      _callback(std::move(callback)) {
}

bool OnceAfterTask::update(uint32_t now) {
    if (now - _startTime >= _delayMs) {
        if (_callback) {
            _callback();
        }
        return false; // виконане один раз - видалити з черги
    }
    return true; // ще чекаємо
}

void OnceAfterTask::onResume(uint32_t pausedForMs) {
    // Зсуваємо старт відліку затримки на час паузи.
    _startTime += pausedForMs;
}
