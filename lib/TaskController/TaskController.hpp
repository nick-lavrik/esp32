#pragma once

#include <memory>
#include <vector>

#include "ITask.h"
#include "TaskCallback.h"

// Черга завдань. Завдання можна додавати динамічно (в т.ч. з колбеків
// інших завдань), TaskController::loop() потрібно викликати регулярно
// з loop() скетчу.
//
// Пам'ять: черга володіє завданнями через std::unique_ptr. Коли
// завдання завершується або видаляється, unique_ptr автоматично
// звільняє пам'ять - без ручного delete і без leaks.
class TaskController {
public:
  TaskController() = default;

  // Тип 1: аналог cron. Виконує callback кожні intervalMs,
  // живе, доки не буде видалене через removeTask(id).
  // Повертає ID для подальшого видалення.
  TaskId addCronTask(uint32_t intervalMs, TaskCallback callback);

  // Тип 2: виконує callback постійно, поки не мине durationMs,
  // після чого автоматично видаляється з черги.
  // Повертає ID (можна видалити достроково через removeTask(id)).
  TaskId addJob(uint32_t durationMs, TaskCallback callback, uint32_t intervalMs = 0);

  // Тип 3: Виконати callback РІВНО ОДИН РАЗ через delayMs, після чого
  // завдання автоматично видаляється з черги.
  // Повертає ID (можна скасувати достроково через removeTask(id)).
  TaskId runOnceAfterMs(uint32_t delayMs, TaskCallback callback);

  // Додати вже готове власне завдання (нащадок ITask).
  TaskId addTask(std::unique_ptr<ITask> task);

  // Видалити завдання з черги за ID (напр. cron-завдання).
  // Реальне видалення відбувається на наступному loop().
  // Повертає true, якщо завдання з таким ID знайдено.
  bool removeTask(TaskId id);

  // Поставити завдання на паузу: воно залишається в черзі, але
  // TaskController перестає викликати його update() - виконання
  // "заморожене" до resume(). Час на паузі не враховується в
  // таймерах завдання. Повертає true, якщо завдання знайдено.
  bool pause(TaskId id);

  // Відновити виконання завдання, поставленого на паузу.
  // Повертає true, якщо завдання знайдено.
  bool resume(TaskId id);

  // Викликати регулярно з loop() скетчу: виконує та прибирає
  // завершені/видалені завдання з черги.
  void loop();

  size_t taskCount() const;

private:
  std::vector<std::unique_ptr<ITask>> _tasks;
  TaskId _nextId = 1;
};
