#pragma once

#include <Event.hpp>

class LightSensorChangedEvent : public Event {
public:
    LightSensorChangedEvent(const int value) : Event(), _value(value) {};
    int value() { return _value; }
private:
    int _value;
};