#pragma once

#include "ITask.h"
#include "TaskCallback.h"

// Тип 2: виконує callback ПОСТІЙНО, поки не мине durationMs,
// після чого автоматично видаляється з черги.
//
// intervalMs - як часто викликати callback усередині цієї тривалості
// (0 = на кожному TaskController::loop()).
class JobTask : public ITask {
public:
    JobTask(uint32_t durationMs, TaskCallback callback, uint32_t intervalMs = 0);

    bool update(uint32_t now) override;

protected:
    void onResume(uint32_t pausedForMs) override;

private:
    uint32_t _startTime;
    uint32_t _durationMs;
    uint32_t _intervalMs;
    uint32_t _lastCallTime;
    TaskCallback _callback;
};
