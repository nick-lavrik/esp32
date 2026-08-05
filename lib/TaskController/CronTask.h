#pragma once

#include "ITask.h"
#include "TaskCallback.h"

// Тип 1: аналог cron. Виконує callback кожні intervalMs і
// НІКОЛИ не видаляється сам - живе, доки TaskController::removeTask(id)
// не видалить його явно.
class CronTask : public ITask {
public:
  CronTask(uint32_t intervalMs, TaskCallback callback);

  bool update(uint32_t now) override;

protected:
  void onResume(uint32_t pausedForMs) override;

private:
  uint32_t _intervalMs;
  uint32_t _lastRun;
  TaskCallback _callback;
};
