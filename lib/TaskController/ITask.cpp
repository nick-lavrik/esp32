#include "ITask.h"

#include <Arduino.h> // millis()

TaskId ITask::id() const {
    return _id;
}

void ITask::setId(TaskId id) {
    _id = id;
}

void ITask::cancel() {
    _cancelled = true;
}

bool ITask::isCancelled() const {
    return _cancelled;
}

void ITask::pause() {
    if (_paused) {
        return; // вже на паузі
    }
    _paused = true;
    _pauseStartedAt = millis();
}

void ITask::resume() {
    if (!_paused) {
        return; // не було на паузі
    }
    _paused = false;
    const uint32_t pausedForMs = millis() - _pauseStartedAt;
    onResume(pausedForMs);
}

bool ITask::isPaused() const {
    return _paused;
}

void ITask::onResume(uint32_t /*pausedForMs*/) {
    // За замовчуванням нічого зсувати не треба - перевизначається
    // підкласами, що мають власні абсолютні мітки часу.
}
