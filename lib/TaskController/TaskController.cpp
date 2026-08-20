#include "TaskController.hpp"

#include <Arduino.h>  // millis()

#include <algorithm>
#include <cassert>
#include <utility>

#include "CronTask.h"
#include "JobTask.h"
#include "OnceAfterTask.h"

TaskId TaskController::addCronTask(uint32_t intervalMs, TaskCallback callback) {
  return addTask(std::make_unique<CronTask>(intervalMs, std::move(callback)));
}

TaskId TaskController::addJob(uint32_t durationMs, TaskCallback callback, uint32_t intervalMs) {
  return addTask(std::make_unique<JobTask>(durationMs, std::move(callback), intervalMs));
}

TaskId TaskController::runOnceAfterMs(uint32_t delayMs, TaskCallback callback) {
  return addTask(std::make_unique<OnceAfterTask>(delayMs, std::move(callback)));
}

TaskId TaskController::addTask(std::unique_ptr<ITask> task) {
  const TaskId id = _nextId++;
  task->setId(id);
  _tasks.push_back(std::move(task));
  return id;
}

bool TaskController::removeTask(TaskId id) {
  for (auto& task : _tasks) {
    if (task->id() == id) {
      task->cancel();
      return true;
    }
  }
  return false;
}

bool TaskController::pause(TaskId id) {
  for (auto& task : _tasks) {
    if (task->id() == id) {
      task->pause();
      return true;
    }
  }
  return false;
}

bool TaskController::isPaused(TaskId id) {
  for (auto& task : _tasks) {
    if (task->id() == id) {
      return task->isPaused();
    }
  }
  // assert(false && ...), а не assert("..."): рядковий літерал завжди
  // істинний, тобто попередній варіант не спрацював би НІКОЛИ.
  assert(false && "isPaused(): unknown TaskId");
  return false;
}

bool TaskController::resume(TaskId id) {
  for (auto& task : _tasks) {
    if (task->id() == id) {
      task->resume();
      return true;
    }
  }
  return false;
}

bool TaskController::isCancelled(TaskId id) {
  for (auto& task : _tasks) {
    if (task->id() == id) {
      return task->isCancelled();
    }
  }
  assert(false && "isCancelled(): unknown TaskId");
  return false;
}

void TaskController::loop() {
  const uint32_t now = millis();

  // Два окремі проходи - навмисно.
  //
  // Раніше тут був один std::remove_if, предикат якого викликав
  // task->update(now), тобто КОРИСТУВАЦЬКИЙ колбек. Якщо колбек додавав нове
  // завдання (addCronTask/addJob/runOnceAfterMs -> push_back у _tasks), вектор
  // міг реалокуватись, а remove_if продовжував працювати з власними, уже
  // недійсними ітераторами - UB. Коментар у .hpp стверджував протилежне.
  //
  // Прохід 1: виконання, ЗА ІНДЕКСОМ до зафіксованого на вході розміру.
  // Індексація переживає реалокацію; сам об'єкт завдання (ITask) при
  // реалокації не рухається - рухаються лише unique_ptr-и, тому взятий
  // заздалегідь ITask* лишається валідним. Завдання, додані з колбека,
  // свідомо не виконуються в цьому ж проході - дочекаються наступного loop()
  // (та сама поведінка, що документована в .hpp).
  const size_t count = _tasks.size();
  for (size_t i = 0; i < count; ++i) {
    ITask* task = _tasks[i].get();

    if (task->isCancelled() || task->isPaused()) {
      continue;  // cancelled приберемо нижче, paused лишається в черзі
    }

    if (!task->update(now)) {
      task->cancel();  // завершилось саме - позначаємо на видалення
    }
  }

  // Прохід 2: компактизація. Предикат не викликає жодного користувацького
  // коду, тому ітератори тут гарантовано валідні.
  _tasks.erase(
      std::remove_if(_tasks.begin(), _tasks.end(),
                     [](const std::unique_ptr<ITask>& task) { return task->isCancelled(); }),
      _tasks.end());
}

size_t TaskController::taskCount() const { return _tasks.size(); }
