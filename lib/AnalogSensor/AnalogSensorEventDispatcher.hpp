#pragma once

#include <Arduino.h>

#include "AnalogSensor.hpp"
#include "AnalogSensorEvent.hpp"
#include "EventDispatcher.hpp"

/**
 * AnalogSensorEventDispatcher
 *
 * Watches an AnalogSensor and dispatches an AnalogSensorValue event through
 * EventDispatcher whenever the reading changes by more than
 * `deltaThresholdPercent` since the last dispatched value.
 *
 * Call `update()` frequently (e.g. from a CronTask running every ~200-500ms).
 * Does not own the AnalogSensor or EventDispatcher (constructor injection by
 * reference), matching the project's existing DI convention.
 */
class AnalogSensorEventDispatcher {
public:
  static constexpr const char* EVT_ANALOG_SENSOR_VALUE = "AnalogSensor.value";
  static constexpr const char* kEventName = "AnalogSensorValue";

  AnalogSensorEventDispatcher(AnalogSensor& sensor, EventDispatcher& eventDispatcher,
                              uint8_t deltaThresholdPercent = 5);

  // Reads the sensor and dispatches AnalogSensorValue if it changed enough.
  void update();

  void setDeltaThresholdPercent(uint8_t deltaThresholdPercent) {
    _deltaThresholdPercent = deltaThresholdPercent;
  }

private:
  bool _hasChangedEnough(uint8_t currentPercent) const;

  AnalogSensor& _sensor;
  EventDispatcher& _eventDispatcher;
  uint8_t _deltaThresholdPercent;

  uint8_t _lastDispatchedPercent = 0;
  bool _hasDispatched = false;
};
