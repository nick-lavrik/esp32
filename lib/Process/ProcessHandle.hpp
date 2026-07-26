#pragma once

#include <memory>

#include "ProcessState.hpp"
#include "ProcessStatus.hpp"

/**
 * @brief Хендл, який отримує викликач Process::doAsyncTask() / doAsyncApp().
 *
 * Дешевий для копіювання (тонка обгортка над std::shared_ptr<ProcessState>).
 * Можна зберігати декілька копій (наприклад, у std::vector<ProcessHandle<T>>)
 * для одночасного відстеження багатьох процесів.
 */
template <typename TProgress>
class ProcessHandle {
public:
    ProcessHandle() = default;
    explicit ProcessHandle(std::shared_ptr<ProcessState<TProgress>> state)
        : _state(std::move(state)) {}

    bool valid() const { return static_cast<bool>(_state); }

    ProcessStatus status() const {
        return valid() ? _state->status.load(std::memory_order_acquire) : ProcessStatus::Pending;
    }

    bool isRunning() const { return status() == ProcessStatus::Running; }

    bool isDone() const {
        auto s = status();
        return s == ProcessStatus::Completed || s == ProcessStatus::Cancelled || s == ProcessStatus::Failed;
    }

    /// Прочитати наступний проміжний результат (неблокуюче). Викликати в циклі,
    /// доки повертає true, щоб вибрати всі накопичені елементи черги.
    bool tryGetProgress(TProgress& out) {
        return valid() && _state->progressQueue.tryPop(out);
    }

    /// Прочитати фінальний результат. Має сенс лише після isDone() зі статусом Completed.
    bool tryGetResult(TProgress& out) const {
        return valid() && _state->getFinalResult(out);
    }

    /// Запросити скасування. Сам процес має перевіряти ctx.isCancelled()
    /// і коректно завершити роботу (викликавши ctx.acknowledgeCancel()).
    void cancel() {
        if (valid()) _state->cancelRequested.store(true, std::memory_order_relaxed);
    }

private:
    std::shared_ptr<ProcessState<TProgress>> _state;
};
