#pragma once

#include <cstdint>
#include <functional>

#include "IEvent.hpp"

// Непрозорий ідентифікатор зареєстрованого слухача. Повертається з
// EventDispatcher::addListener() і використовується для подальшого
// removeListener(id) - аналог TaskId у TaskController.
using ListenerId = uint32_t;

// "Порожній"/невалідний ListenerId - жоден реальний listener його не отримає.
constexpr ListenerId kInvalidListenerId = 0;

// Сигнатура, якій має відповідати кожен слухач події.
using EventListener = std::function<void(IEvent&)>;
