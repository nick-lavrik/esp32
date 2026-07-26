#pragma once

extern "C" {
#include "freertos/FreeRTOS.h"
}

#include <atomic>

#include "ProcessResultQueue.hpp"
#include "ProcessStatus.hpp"

/**
 * @brief Внутрішній спільний стан одного процесу.
 *
 * Живе, доки живий хоча б один власник std::shared_ptr<ProcessState> —
 * а саме: сама задача/пампа під час виконання, і ProcessHandle на боці
 * користувача. Коли обидва власники звільнять shared_ptr, стан і черга
 * коректно звільняються автоматично (RAII).
 *
 * Розділяється між ProcessContext (пише) та ProcessHandle (читає).
 */
template <typename TProgress>
class ProcessState {
public:
    explicit ProcessState(UBaseType_t queueCapacity = 8)
        : progressQueue(queueCapacity) {}

    std::atomic<ProcessStatus> status{ProcessStatus::Pending};
    std::atomic<bool>          cancelRequested{false};

    ProcessResultQueue<TProgress> progressQueue;

    /// Записати фінальний результат потокобезпечно (spinlock, безпечно між ядрами).
    void setFinalResult(const TProgress& value) {
        portENTER_CRITICAL(&_finalResultLock);
        _finalResult = value;
        portEXIT_CRITICAL(&_finalResultLock);
        _hasFinalResult.store(true, std::memory_order_release);
    }

    /// Прочитати фінальний результат. false, якщо ще не встановлено.
    bool getFinalResult(TProgress& out) const {
        if (!_hasFinalResult.load(std::memory_order_acquire)) return false;
        portENTER_CRITICAL(&_finalResultLock);
        out = _finalResult;
        portEXIT_CRITICAL(&_finalResultLock);
        return true;
    }

private:
    mutable portMUX_TYPE _finalResultLock = portMUX_INITIALIZER_UNLOCKED;
    std::atomic<bool>    _hasFinalResult{false};
    TProgress            _finalResult{};
};
