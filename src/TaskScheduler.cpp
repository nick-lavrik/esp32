#include "TaskScheduler.h"

#include <Arduino.h> // millis()
#include <algorithm>
#include <utility>

#include "CronTask.h"
#include "JobTask.h"
#include "OnceAfterTask.h"

TaskId TaskScheduler::addCronTask(uint32_t intervalMs, TaskCallback callback) {
    return addTask(std::make_unique<CronTask>(intervalMs, std::move(callback)));
}

TaskId TaskScheduler::addJob(uint32_t durationMs, TaskCallback callback, uint32_t intervalMs) {
    return addTask(std::make_unique<JobTask>(durationMs, std::move(callback), intervalMs));
}

TaskId TaskScheduler::runOnceAfterMs(uint32_t delayMs, TaskCallback callback) {
    return addTask(std::make_unique<OnceAfterTask>(delayMs, std::move(callback)));
}

TaskId TaskScheduler::addTask(std::unique_ptr<ITask> task) {
    const TaskId id = _nextId++;
    task->setId(id);
    _tasks.push_back(std::move(task));
    return id;
}

bool TaskScheduler::removeTask(TaskId id) {
    for (auto& task : _tasks) {
        if (task->id() == id) {
            task->cancel();
            return true;
        }
    }
    return false;
}

void TaskScheduler::loop() {
    const uint32_t now = millis();

    // Важливо: якщо колбек завдання сам додає нові завдання в чергу
    // (addCronTask/addJob всередині колбека) - це безпечно, нові
    // елементи потрапляють у vector і будуть оброблені на наступному
    // виклику loop().
    _tasks.erase(
        std::remove_if(_tasks.begin(), _tasks.end(),
                        [now](const std::unique_ptr<ITask>& task) {
                            if (task->isCancelled()) {
                                return true;
                            }
                            return !task->update(now);
                        }),
        _tasks.end());
}

size_t TaskScheduler::taskCount() const {
    return _tasks.size();
}
