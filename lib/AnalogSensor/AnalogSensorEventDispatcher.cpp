#include "AnalogSensorEventDispatcher.hpp"

AnalogSensorEventDispatcher::AnalogSensorEventDispatcher(AnalogSensor& sensor,
                                                          EventDispatcher& eventDispatcher,
                                                          uint8_t deltaThresholdPercent)
    : _sensor(sensor), _eventDispatcher(eventDispatcher), _deltaThresholdPercent(deltaThresholdPercent) {
}

void AnalogSensorEventDispatcher::update() {
    _sensor.update();

    if (_hasChangedEnough(_sensor.percent())) {
        AnalogSensorEvent event(_sensor, _sensor.percent(), _sensor.value(), millis());
        _eventDispatcher.dispatch(event, EVT_ANALOG_SENSOR_VALUE);

        _lastDispatchedPercent = _sensor.percent();
        _hasDispatched = true;
    }
}

bool AnalogSensorEventDispatcher::_hasChangedEnough(uint8_t currentPercent) const {
    if (!_hasDispatched) {
        return true;
    }

    uint8_t delta = (currentPercent > _lastDispatchedPercent)
        ? (currentPercent - _lastDispatchedPercent)
        : (_lastDispatchedPercent - currentPercent);

    return delta >= _deltaThresholdPercent;
}
