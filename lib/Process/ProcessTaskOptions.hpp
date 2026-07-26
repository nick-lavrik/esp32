#pragma once

extern "C" {
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
}

/**
 * @brief Налаштування для Process::doAsyncTask() (окрема FreeRTOS-задача).
 *
 * Приклад:
 * @code
 * ProcessTaskOptions opts;
 * opts.name = "sensorTask";
 * opts.stackSize = 8192;
 * opts.priority = 2;
 * opts.coreId = 1;
 * @endcode
 */
struct ProcessTaskOptions {
    const char* name      = "procTask";           ///< Ім'я задачі (для vTaskList/діагностики)
    uint32_t    stackSize = 4096;                 ///< Розмір стеку в БАЙТАХ (на ESP32 xTaskCreate приймає байти, не слова!)
    UBaseType_t priority  = tskIDLE_PRIORITY + 1;  ///< Пріоритет FreeRTOS-задачі (0..configMAX_PRIORITIES-1)
    BaseType_t  coreId    = tskNO_AFFINITY;        ///< Ядро виконання: 0, 1 або tskNO_AFFINITY (планувальник сам обере)
};
