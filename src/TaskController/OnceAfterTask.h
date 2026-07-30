#pragma once

#include "ITask.h"
#include "TaskCallback.h"

// Виконує callback РІВНО ОДИН РАЗ через delayMs, після чого
// автоматично видаляється з черги.
class OnceAfterTask : public ITask {
public:
    OnceAfterTask(uint32_t delayMs, TaskCallback callback);

    bool update(uint32_t now) override;

protected:
    void onResume(uint32_t pausedForMs) override;

private:
    uint32_t _startTime;
    uint32_t _delayMs;
    TaskCallback _callback;
};
