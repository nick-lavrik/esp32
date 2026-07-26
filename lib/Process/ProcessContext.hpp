#pragma once

#include <memory>

#include "ProcessState.hpp"
#include "ProcessStatus.hpp"

/**
 * @brief Контекст виконання, який передається у функцію користувача.
 *
 * Це ОДИН І ТОЙ САМИЙ інтерфейс для обох моделей виконання:
 *   - Process::doAsyncTask()  -> void(ProcessContext<T>&)
 *   - Process::doAsyncApp()   -> int(ProcessContext<T>&)   (0 = продовжити, 1 = завершено)
 *
 * Користувацький код не повинен знати/дбати, в якій моделі він виконується.
 */
template <typename TProgress>
class ProcessContext {
public:
    explicit ProcessContext(std::shared_ptr<ProcessState<TProgress>> state)
        : _state(std::move(state)) {}

    /// Опублікувати проміжний результат (неблокуюче, потокобезпечне).
    /// false, якщо черга проміжних результатів переповнена (споживач не встигає читати).
    bool report(const TProgress& value) {
        return _state->progressQueue.tryPush(value);
    }

    /// Те саме, але заміщує найстаріший непрочитаний елемент при переповненні.
    /// Зручно, коли важливе лише останнє значення прогресу (наприклад, відсоток).
    bool reportLatest(const TProgress& value) {
        return _state->progressQueue.pushOverwriteIfFull(value);
    }

    /// Чи запросив користувач (через ProcessHandle::cancel()) скасування.
    /// Перевіряти періодично всередині довгої/циклічної роботи.
    bool isCancelled() const {
        return _state->cancelRequested.load(std::memory_order_relaxed);
    }

    /// Завершити процес успішно з фінальним результатом.
    /// Для doAsyncTask: викликати перед виходом з функції.
    /// Для doAsyncApp: викликати і повернути 1 з update-функції.
    void finish(const TProgress& result) {
        _state->setFinalResult(result);
        _state->status.store(ProcessStatus::Completed, std::memory_order_release);
    }

    /// Явно підтвердити скасування (якщо isCancelled() == true і функція
    /// коректно згорнула роботу раніше запланованого).
    void acknowledgeCancel() {
        _state->status.store(ProcessStatus::Cancelled, std::memory_order_release);
    }

    /// Позначити процес як завершений з помилкою.
    void fail() {
        _state->status.store(ProcessStatus::Failed, std::memory_order_release);
    }

    const std::shared_ptr<ProcessState<TProgress>>& state() const { return _state; }

private:
    std::shared_ptr<ProcessState<TProgress>> _state;
};
