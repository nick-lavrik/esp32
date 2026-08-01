#pragma once

#include <cstdint>

// Ідентифікатор завдання в черзі TaskController.
using TaskId = uint32_t;

// Базовий інтерфейс будь-якого завдання в черзі TaskController.
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

    // Позначити завдання для видалення (напр. з TaskController::removeTask).
    // Реальне видалення з черги відбувається на наступному loop().
    void cancel();
    bool isCancelled() const;

    // Поставити завдання на паузу / відновити (без видалення з черги).
    // Поки завдання на паузі, TaskController НЕ викликає його update().
    // Час, проведений на паузі, не враховується в таймерах завдання -
    // це забезпечується через onResume(), який підклас перевизначає,
    // щоб зсунути свої внутрішні мітки часу.
    void pause();
    void resume();
    bool isPaused() const;

protected:
    // Викликається автоматично з resume(). pausedForMs - скільки мс
    // завдання провело на паузі. Підклас, що зберігає власні абсолютні
    // мітки часу (напр. _lastRun, _startTime), має перевизначити цей
    // метод і зсунути їх на pausedForMs, інакше після паузи завдання
    // може одразу "вистрілити" через накопичений реальний час.
    virtual void onResume(uint32_t pausedForMs);

private:
    TaskId _id = 0;
    bool _cancelled = false;
    bool _paused = false;
    uint32_t _pauseStartedAt = 0;
};
