#include "BlinkTask.hpp"

extern "C" {
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
}

#include <Arduino.h>

BlinkTask::BlinkTask(uint8_t pin, int times, uint32_t intervalMs)
    : _pin(pin), _times(times), _intervalMs(intervalMs) {}

void BlinkTask::operator()(ProcessContext<int>& ctx) {
    pinMode(_pin, OUTPUT);

    int completed = 0;
    for (int i = 0; i < _times; ++i) {
        if (ctx.isCancelled()) {
            ctx.acknowledgeCancel();
            return;  // фінальний результат у цьому прикладі не публікуємо при скасуванні
        }

        digitalWrite(_pin, HIGH);
        vTaskDelay(pdMS_TO_TICKS(_intervalMs));
        digitalWrite(_pin, LOW);
        vTaskDelay(pdMS_TO_TICKS(_intervalMs));

        completed++;
        int percent = (completed * 100) / _times;
        ctx.reportLatest(percent);  // достатньо лише останнього значення прогресу
    }

    ctx.finish(completed);
}
