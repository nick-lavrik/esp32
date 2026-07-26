#include "SensorPollApp.hpp"

#include <Arduino.h>

SensorPollApp::SensorPollApp(uint8_t analogPin, uint32_t intervalMs, int sampleCount)
    : _pin(analogPin), _intervalMs(intervalMs), _sampleCount(sampleCount) {}

int SensorPollApp::operator()(ProcessContext<float>& ctx) {
    if (ctx.isCancelled()) {
        ctx.acknowledgeCancel();
        return 1;
    }

    uint32_t now = millis();
    if (!_started) {
        _lastSampleAt = now;
        _started = true;
    }

    // Неблокуюча перевірка інтервалу - жодних delay()/vTaskDelay() тут бути не може.
    if (now - _lastSampleAt < _intervalMs) {
        return 0;  // ще не час наступного виміру - продовжуємо наступного разу
    }
    _lastSampleAt = now;

    float value = analogRead(_pin);
    _sum += value;
    _samplesTaken++;
    ctx.reportLatest(value);

    if (_samplesTaken >= _sampleCount) {
        ctx.finish(_sum / static_cast<float>(_samplesTaken));
        return 1;
    }

    return 0;
}
