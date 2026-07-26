#pragma once

extern "C" {
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
}

#include <type_traits>

/**
 * @brief Потокобезпечна черга проміжних результатів на основі нативної
 *        FreeRTOS-черги (xQueueCreate). Обрана як основний механізм доставки
 *        даних з ProcessContext, оскільки:
 *          - є ISR/thread-safe "з коробки" (реалізовано в самому ядрі FreeRTOS),
 *          - однаково коректно працює як між різними задачами (модель
 *            doAsyncTask), так і в межах одного потоку (модель doAsyncApp),
 *          - не потребує додаткових м'ютексів/спінлоків з боку бібліотеки.
 *
 * ВАЖЛИВО: T має бути trivially copyable (POD-подібний тип), тому що
 * xQueueSend/xQueueReceive копіюють sizeof(T) байт напряму (memcpy).
 *
 * Для "важких" типів (String, std::vector, довільні дані зі своєю
 * купою) НЕ кладіть їх у чергу напряму. Замість цього:
 *   1) на боці виробника: `auto* p = new MyHeavyType(...);`
 *   2) покласти в чергу T = MyHeavyType* (сам вказівник — POD, memcpy безпечний),
 *   3) на боці споживача: прочитати вказівник і ОБОВ'ЯЗКОВО викликати `delete p;`
 *      одразу після використання — власність передається рівно один раз.
 */
template <typename T>
class ProcessResultQueue {
    static_assert(std::is_trivially_copyable<T>::value,
                  "ProcessResultQueue<T>: T має бути trivially copyable "
                  "(для важких типів використовуйте T = вказівник, див. коментар у файлі)");

public:
    explicit ProcessResultQueue(UBaseType_t capacity = 8)
        : _handle(xQueueCreate(capacity, sizeof(T))) {}

    ~ProcessResultQueue() {
        if (_handle != nullptr) {
            vQueueDelete(_handle);
        }
    }

    ProcessResultQueue(const ProcessResultQueue&)            = delete;
    ProcessResultQueue& operator=(const ProcessResultQueue&) = delete;
    ProcessResultQueue(ProcessResultQueue&&)                 = delete;
    ProcessResultQueue& operator=(ProcessResultQueue&&)      = delete;

    bool isValid() const { return _handle != nullptr; }

    /// Неблокуючий запис. false, якщо черга повна або невалідна.
    bool tryPush(const T& value) {
        if (_handle == nullptr) return false;
        return xQueueSend(_handle, &value, 0) == pdTRUE;
    }

    /// Запис із витісненням найстарішого елемента, якщо черга повна.
    /// Зручно для "прогресу", де важливе лише останнє значення.
    bool pushOverwriteIfFull(const T& value) {
        if (_handle == nullptr) return false;
        if (xQueueSend(_handle, &value, 0) == pdTRUE) return true;
        T discarded{};
        xQueueReceive(_handle, &discarded, 0);
        return xQueueSend(_handle, &value, 0) == pdTRUE;
    }

    /// Неблокуюче читання. false, якщо черга порожня.
    bool tryPop(T& out) {
        if (_handle == nullptr) return false;
        return xQueueReceive(_handle, &out, 0) == pdTRUE;
    }

    UBaseType_t available() const {
        return _handle ? uxQueueMessagesWaiting(_handle) : 0;
    }

private:
    QueueHandle_t _handle;
};
