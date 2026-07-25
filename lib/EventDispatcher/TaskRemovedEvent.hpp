#pragma once

#include "Event.hpp"
#include "ITask.h" // звідти TaskId

// Подія: TaskController видалив/скасував завдання.
// dispatch("task.removed", TaskRemovedEvent(id))
class TaskRemovedEvent : public Event {
public:
    explicit TaskRemovedEvent(TaskId id) : _id(id) {}

    TaskId id() const { return _id; }

private:
    TaskId _id;
};
