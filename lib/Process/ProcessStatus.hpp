#pragma once

#include <cstdint>

/**
 * @brief Статус виконання асинхронного процесу.
 *
 * Спільний для обох моделей виконання: Process::doAsyncTask() (FreeRTOS-задача)
 * та Process::doAsyncApp() (кооперативна модель через Process::update()).
 */
enum class ProcessStatus : uint8_t {
  Pending,    ///< Створено, ще не розпочало виконання
  Running,    ///< Виконується
  Completed,  ///< Завершено успішно (ctx.finish() викликано)
  Cancelled,  ///< Скасовано (ProcessHandle::cancel() + ctx.acknowledgeCancel() / автоматично)
  Failed  ///< Завершено з помилкою (ctx.fail() викликано)
};
