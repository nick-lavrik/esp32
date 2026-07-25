#pragma once

#include "IEvent.hpp"

// Базова реалізація IEvent. Власні події проєкту наслідують цей клас
// і додають свої поля (payload) для передачі даних слухачам.
//
// Приклад:
//   class SensorReadyEvent : public Event {
//   public:
//       explicit SensorReadyEvent(float value) : _value(value) {}
//       float value() const { return _value; }
//   private:
//       float _value;
//   };
class Event : public IEvent {
public:
    Event() = default;
    ~Event() override = default;

    void stopPropagation() override;
    bool isPropagationStopped() const override;

private:
    bool _propagationStopped = false;
};
