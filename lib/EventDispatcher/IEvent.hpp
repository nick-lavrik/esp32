#pragma once

// Базовий інтерфейс для всіх подій, що передаються через EventDispatcher.
// Аналог Symfony\Contracts\EventDispatcher\Event.
//
// Власні події проєкту наслідують клас Event (див. Event.hpp), а не
// реалізують IEvent напряму.
class IEvent {
public:
    virtual ~IEvent() = default;

    // Зупиняє подальшу передачу події наступним слухачам.
    virtual void stopPropagation() = 0;

    // Повертає true, якщо передачу події вже зупинено.
    virtual bool isPropagationStopped() const = 0;
};
