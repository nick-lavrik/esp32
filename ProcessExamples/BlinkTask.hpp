#pragma once

#include <cstdint>

#include "Process.hpp"

/**
 * @brief Приклад класу-задачі для Process::doAsyncTask<int>().
 *
 * Блимає світлодіодом задану кількість разів У ВЛАСНІЙ FreeRTOS-задачі,
 * публікуючи прогрес (0-100%) через ctx.reportLatest() і фінальний результат -
 * фактичну кількість виконаних блимань (може бути меншою за задану, якщо
 * процес скасовано через ProcessHandle::cancel()).
 *
 * Об'єкт класу - callable (визначає operator()), тому
 * std::function<void(ProcessContext<int>&)> приймає його напряму так само,
 * як звичайну лямбду:
 *
 * @code
 * ProcessHandle<int> h = Process::doAsyncTask<int>(BlinkTask(2, 10, 150));
 * @endcode
 */
class BlinkTask {
public:
    BlinkTask(uint8_t pin, int times, uint32_t intervalMs);

    /// Викликається планувальником FreeRTOS у власній задачі (Process::taskTrampoline).
    void operator()(ProcessContext<int>& ctx);

private:
    uint8_t  _pin;
    int      _times;
    uint32_t _intervalMs;
};
