#pragma once

#include <Arduino.h>

using AnalogSensorListener = std::function<void()>;
using AnalogSensorListenerId = uint32_t;

/**
 * AnalogSensor
 *
 * Generic reader for any analog (ADC-based) sensor: photoresistor/LDR,
 * potentiometer, soil moisture probe, gas sensor, etc. Applies an
 * exponential moving average (EMA) to smooth out noise from cheap
 * sensors/dividers.
 *
 * Platform-agnostic: only needs an ADC-capable GPIO, works identically on
 * classic ESP32 and ESP32-S3 boards. The pin is injected via the
 * constructor — no board-specific pin maps are hardcoded here.
 */
class AnalogSensor {
public:
    // adcMaxValue: 4095 for 12-bit ADC (default on both ESP32 and ESP32-S3 with default analogReadResolution).
    // 12 бит (по умолчанию): выдает значения 0–4095 (всего 4096 шагов).
    // 11 бит: выдает значения 0–2047.
    // 10 бит: выдает значения 0–1023 (как на Arduino Uno).
    // 9 бит: выдает значения 0–511.
    explicit AnalogSensor(uint8_t adcPin, float emaAlpha = 0.2f, uint16_t adcMaxValue = 4095, uint8_t mapPercentMin = 0, uint8_t mapPercentMax = 100)
        : _adcPin(adcPin), _emaAlpha(emaAlpha), _adcMaxValue(adcMaxValue), mapPercentMin(mapPercentMin), mapPercentMax(mapPercentMax) {
    }

    void begin() {
        pinMode(_adcPin, INPUT);
        update();
    }

    // Call periodically (e.g. from a CronTask) to take a new reading.
    void update() {


        
        uint16_t actual = analogRead(_adcPin);
        if (actual != _rawValue) {
            _rawValue = actual;
            notify();
        }

        if (!_hasReading) {
            _filteredValue = static_cast<float>(_rawValue);
            _hasReading = true;
        } else {
            _filteredValue = _emaAlpha * static_cast<float>(_rawValue) + (1.0f - _emaAlpha) * _filteredValue;
        }
    }

    void notify() {
        for (const auto& entry : _listeners) {
           entry.listener();
        }
    }

    const uint16_t rawValue() const { return _rawValue; }
    const uint16_t value() const { return rawValue(); }

    uint16_t filteredValue() const { return static_cast<uint16_t>(_filteredValue + 0.5f); }

    // Maps the filtered reading to 0-100. Assumes a higher ADC value means a
    // higher reading on whatever scale the sensor represents (more light,
    // more moisture, etc.); use `100 - percent()` if your wiring is inverted.
    const uint8_t percent() const {
        // int currentSensorPercent = constrain(map(raw, 1500, 0, 1, 100), 1, 100);
        float p = (_filteredValue / static_cast<float>(_adcMaxValue)) * 100.0f;
        if (p < 0.0f) p = 0.0f;
        if (p > 100.0f) p = 100.0f;
        return map(static_cast<uint8_t>(p + 0.5f), mapPercentMin, mapPercentMax, 0, 100);
    }

    AnalogSensorListenerId addListener(AnalogSensorListener listener) {
        const AnalogSensorListenerId id = _nextId++;
        _listeners.push_back(Entry{id, std::move(listener)});
        // _listeners.push_back(Entry{id, listener});

        return id;
    }
private:

    uint8_t _adcPin;
    uint16_t _rawValue = 0;

    float _emaAlpha;
    uint16_t _adcMaxValue;

    float _filteredValue = 0.0f;
    bool _hasReading = false;
    uint8_t mapPercentMin = 0;
    uint8_t mapPercentMax = 100;

private:
    struct Entry {
        AnalogSensorListenerId id;
        AnalogSensorListener listener;
    };

    std::vector<Entry> _listeners;
    AnalogSensorListenerId _nextId = 1;
};
