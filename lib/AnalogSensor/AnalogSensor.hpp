#pragma once

#include <Arduino.h>

#include <algorithm>
#include <atomic>
#include <functional>
#include <vector>

using AnalogSensorListener = std::function<void()>;
using AnalogSensorListenerId = uint32_t;

/**
 * AnalogSensor
 *
 * Generic reader for any analog (ADC-based) sensor: photoresistor/LDR,
 * potentiometer, soil moisture probe, gas sensor, etc.
 *
 * Platform-agnostic: only needs an ADC-capable GPIO, works identically on
 * classic ESP32 and ESP32-S3 boards. The pin is injected via the
 * constructor — no board-specific pin maps are hardcoded here.
 *
 * AnalogSensorListener - caller must call removeListener() before the
 * listener's captured context is destroyed.
 *
 * TODO: add mutex in update() / addListener() / removeListener()
 * safe FreeRTOS task (e.g. AsyncPinger)
 */
class AnalogSensor {
public:
  // adcMaxValue: 4095 for 12-bit ADC (default on both ESP32 and ESP32-S3 with default
  // analogReadResolution). 12 бит (по умолчанию): выдает значения 0–4095 (всего 4096 шагов). 11
  // бит: выдает значения 0–2047. 10 бит: выдает значения 0–1023 (как на Arduino Uno). 9 бит: выдает
  // значения 0–511.
  explicit AnalogSensor(uint8_t adcPin, uint16_t adcMinValue = 0, uint16_t adcMaxValue = 4095,
                        uint16_t mapMin = 0, uint16_t mapMax = 4095, uint16_t mapThreshold = 1)
      : _adcPin(adcPin),
        _adcMinValue(adcMinValue),
        _adcMaxValue(adcMaxValue),
        _mapMin(mapMin),
        _mapMax(mapMax),
        _mapThreshold(mapThreshold) {
    assert(adcMinValue != adcMaxValue);
  }

  void begin() {
    pinMode(_adcPin, INPUT);
    update();
  }

  uint16_t read() const { return _raw; }
  uint16_t value() const { return _value; }

  // Call periodically (e.g. from a CronTask) to take a new reading.
  void update() {
    // lock_free прапорець, спільний для всіх викликів методу
    static std::atomic_flag isExecuting = ATOMIC_FLAG_INIT;

    // test_and_set повертає true, якщо прапорець ВЖЕ був встановлений
    if (isExecuting.test_and_set(std::memory_order_acquire)) {
      return;  // Забороняємо рекурсію або паралельне виконання
    }

    // raw analag sensor value
    uint16_t raw = analogRead(_adcPin);
    // raw "percentage" (normalised) sensor value
    long value = map(raw, _adcMinValue, _adcMaxValue, _mapMin, _mapMax);
    // safe "percentage" (normilised) sensor value
    uint16_t actual = static_cast<uint16_t>(
        constrain(value, std::min(_mapMin, _mapMax), std::max(_mapMin, _mapMax)));

    _raw = raw;

    if (abs(actual - _value) >= _mapThreshold) {
      _value = actual;
      notify();
    }

    // Звільняємо прапорець
    isExecuting.clear(std::memory_order_release);
  }

  AnalogSensorListenerId addListener(AnalogSensorListener listener) {
    const AnalogSensorListenerId id = _nextId++;
    _listeners.push_back(Entry{id, std::move(listener)});

    return id;
  }

  void removeListener(AnalogSensorListenerId id) {
    _listeners.erase(std::remove_if(_listeners.begin(), _listeners.end(),
                                    [id](const Entry& entry) { return entry.id == id; }),
                     _listeners.end());
  }

private:
  void notify() {
    std::vector<Entry> snapshot = _listeners;  // копія на момент виклику
    for (const auto& entry : snapshot) {
      if (entry.listener) {
        entry.listener();
      }
    }
  }

  const uint8_t _adcPin;
  uint16_t _raw = 0;
  uint16_t _value = 0;

  const uint16_t _adcMinValue;
  const uint16_t _adcMaxValue;

  const uint16_t _mapMin;
  const uint16_t _mapMax;
  uint16_t _mapThreshold;

  struct Entry {
    AnalogSensorListenerId id;
    AnalogSensorListener listener;
  };

  std::vector<Entry> _listeners;
  AnalogSensorListenerId _nextId = 1;
};
