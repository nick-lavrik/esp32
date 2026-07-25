#include "ITask.h"

TaskId ITask::id() const {
    return _id;
}

void ITask::setId(TaskId id) {
    _id = id;
}

void ITask::cancel() {
    _cancelled = true;
}

bool ITask::isCancelled() const {
    return _cancelled;
}
