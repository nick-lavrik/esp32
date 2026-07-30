#pragma once

// #include <Arduino.h>
#include "Event.hpp"

/**
 * AnalogSensorValue
 *
 * Event dispatched by AnalogSensorEventDispatcher whenever an AnalogSensor
 * reading changes by more than its configured threshold. Carries both the
 * normalized percent reading and the raw ADC value, so listeners can pick
 * whichever fits their needs.
 */
class AnalogSensorEvent : public Event {
public:
    AnalogSensorEvent(const AnalogSensor& sensor, const uint16_t percent, const uint16_t rawValue, const unsigned long timestampMs)
        : Event(), sensor(sensor), percent(percent), rawValue(rawValue), timestampMs(timestampMs) {
    }

    const AnalogSensor& sensor;
    const uint8_t percent;
    const uint16_t rawValue;
    const unsigned long timestampMs;

    const uint16_t value() { return rawValue; };
};
