// Внутрішній singleton-реєстр PrintQueue за адресою Print&-виходу.
// Не використовувати напряму з коду логера - тільки через
// PrintQueue::flush() (дренаж) і PrintQueueRegistry::forOutput()
// (SerialLogger сам звертається за конкретною чергою).
#pragma once

#include <Print.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <memory>
#include <unordered_map>

#include "PrintQueue.hpp"

class PrintQueueRegistry {
public:
    static PrintQueueRegistry& instance();

    // Повертає чергу для output, створюючи її при першому зверненні.
    PrintQueue& forOutput(Print& output);

    // Дренажить усі зареєстровані черги.
    void flushAll();

private:
    PrintQueueRegistry();

    SemaphoreHandle_t _mapMutex;
    StaticSemaphore_t _mapMutexBuffer;
    std::unordered_map<void*, std::unique_ptr<PrintQueue>> _queues;
};
