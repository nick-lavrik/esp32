#pragma once

#include <cstdint>

// Ідентифікатор завдання в черзі TaskScheduler.
using TaskId = uint32_t;

// Базовий інтерфейс будь-якого завдання в черзі TaskScheduler.
class ITask {
public:
    virtual ~ITask() = default;

    // Викликається планувальником на кожному loop().
    // now - поточний час у мс (millis()).
    // Повертає true, якщо завдання має залишитись у черзі,
    // false - якщо завдання завершене і його треба видалити.
    virtual bool update(uint32_t now) = 0;

    TaskId id() const;
    void setId(TaskId id);

    // Позначити завдання для видалення (напр. з TaskScheduler::removeTask).
    // Реальне видалення з черги відбувається на наступному loop().
    void cancel();
    bool isCancelled() const;

private:
    TaskId _id = 0;
    bool _cancelled = false;
};
