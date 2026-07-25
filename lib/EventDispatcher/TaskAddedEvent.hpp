#pragma once

#include "Event.hpp"
#include "ITask.h" // звідти TaskId

// Подія: TaskController додав нове завдання в чергу.
// dispatch("task.added", TaskAddedEvent(id))
class TaskAddedEvent : public Event {
public:
    explicit TaskAddedEvent(TaskId id) : _id(id) {}

    TaskId id() const { return _id; }

private:
    TaskId _id;
};
