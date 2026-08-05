#pragma once

extern "C" {
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
}

#include <functional>
#include <memory>
#include <vector>

#include "ProcessContext.hpp"
#include "ProcessHandle.hpp"
#include "ProcessState.hpp"
#include "ProcessStatus.hpp"
#include "ProcessTaskOptions.hpp"

/**
 * @brief Статичний фасад для запуску асинхронних (non-blocking) процесів.
 *
 * Дві моделі виконання, спільний ProcessContext<T>:
 *
 *  1) Process::doAsyncTask<T>(fn, options)
 *     - Створює ОКРЕМУ FreeRTOS-задачу (xTaskCreatePinnedToCore) на кожен виклик.
 *     - Справжня паралельність (в т.ч. між ядрами).
 *     - fn: std::function<void(ProcessContext<T>&)> — звичайна "довга" функція.
 *     - Динамічний пул: без ліміту кількості, кожен виклик = нова задача,
 *       яка сама себе видаляє (vTaskDelete) по завершенню.
 *
 *  2) Process::doAsyncApp<T>(updateFn)
 *     - Кооперативна модель: жодної нової задачі не створюється.
 *     - updateFn: std::function<int(ProcessContext<T>&)>, викликається один раз
 *       за кожен виклик Process::update() (який має викликатись з loop()).
 *       Повертає 0 -> продовжуємо (наступного разу updateFn викликається знову),
 *       1 -> процес завершено, автоматично прибирається з реєстру.
 *     - Динамічний реєстр (std::vector), без ліміту кількості одночасних процесів.
 *
 * Приклад використання дивіться в README.md бібліотеки.
 */
class Process {
public:
  Process() = delete;  // лише статичні методи

  /// Запустити процес в окремій FreeRTOS-задачі.
  template <typename TProgress>
  static ProcessHandle<TProgress> doAsyncTask(
      std::function<void(ProcessContext<TProgress>&)> fn,
      const ProcessTaskOptions& options = ProcessTaskOptions{});

  /// Зареєструвати кооперативний процес, що "прокачується" через Process::update().
  template <typename TProgress>
  static ProcessHandle<TProgress> doAsyncApp(
      std::function<int(ProcessContext<TProgress>&)> updateFn);

  /// Викликати з loop() / основного циклу програми. Прокачує на один крок
  /// вперед усі кооперативні процеси, зареєстровані через doAsyncApp().
  /// НЕ впливає на процеси, запущені через doAsyncTask() (вони виконуються
  /// незалежно у власних FreeRTOS-задачах).
  static void update();

  /// Кількість активних (ще не завершених) кооперативних процесів у реєстрі.
  static size_t activeAppCount();

private:
  // --- Внутрішня "стерта за типом" (type-erased) обгортка для реєстру doAsyncApp ---
  struct AppPumpEntry {
    std::function<bool()> pump;  ///< true = процес завершено, прибрати з реєстру
    std::atomic<bool> done{false};
  };

  // --- Дані для передачі в FreeRTOS-задачу (doAsyncTask) ---
  template <typename TProgress>
  struct TaskLaunchData {
    std::shared_ptr<ProcessState<TProgress>> state;
    std::function<void(ProcessContext<TProgress>&)> fn;
  };

  template <typename TProgress>
  static void taskTrampoline(void* param);

  static void registerAppPump(std::function<bool()> pump);

  // Реалізовано в Process.cpp (Meyer's singleton, уникає fiasco статичної ініціалізації).
  static std::vector<std::shared_ptr<AppPumpEntry>>& registry();
};

// ============================== Реалізація шаблонних методів ==============================

template <typename TProgress>
void Process::taskTrampoline(void* param) {
  auto* launch = static_cast<TaskLaunchData<TProgress>*>(param);
  auto state = launch->state;  // локальна копія shared_ptr - тримає стан живим під час виконання
  ProcessContext<TProgress> ctx(state);

  state->status.store(ProcessStatus::Running, std::memory_order_release);
  launch->fn(ctx);

  // Якщо користувацька функція не викликала finish()/fail()/acknowledgeCancel() явно -
  // визначаємо фінальний статус автоматично.
  if (state->status.load(std::memory_order_acquire) == ProcessStatus::Running) {
    if (state->cancelRequested.load(std::memory_order_relaxed)) {
      state->status.store(ProcessStatus::Cancelled, std::memory_order_release);
    } else {
      state->status.store(ProcessStatus::Completed, std::memory_order_release);
    }
  }

  delete launch;
  vTaskDelete(nullptr);
}

template <typename TProgress>
ProcessHandle<TProgress> Process::doAsyncTask(std::function<void(ProcessContext<TProgress>&)> fn,
                                              const ProcessTaskOptions& options) {
  auto state = std::make_shared<ProcessState<TProgress>>();
  auto* launch = new TaskLaunchData<TProgress>{state, std::move(fn)};

  TaskHandle_t taskHandle = nullptr;
  BaseType_t ok =
      xTaskCreatePinnedToCore(&Process::taskTrampoline<TProgress>, options.name, options.stackSize,
                              launch, options.priority, &taskHandle, options.coreId);

  if (ok != pdPASS) {
    // Не вдалося створити задачу (найімовірніше - брак пам'яті на стек).
    delete launch;
    state->status.store(ProcessStatus::Failed, std::memory_order_release);
  }

  return ProcessHandle<TProgress>(state);
}

template <typename TProgress>
ProcessHandle<TProgress> Process::doAsyncApp(
    std::function<int(ProcessContext<TProgress>&)> updateFn) {
  auto state = std::make_shared<ProcessState<TProgress>>();
  state->status.store(ProcessStatus::Running, std::memory_order_release);

  // Захоплюємо typed state/fn у лямбді, яка повертає bool (стерта за типом сигнатура).
  auto pump = [state, fn = std::move(updateFn)]() mutable -> bool {
    if (state->status.load(std::memory_order_acquire) != ProcessStatus::Running) {
      return true;  // хтось ззовні вже перевів статус у фінальний
    }

    ProcessContext<TProgress> ctx(state);
    int result = fn(ctx);

    if (state->status.load(std::memory_order_acquire) != ProcessStatus::Running) {
      return true;  // fn сама викликала finish()/fail()/acknowledgeCancel()
    }

    if (result != 0) {
      if (state->cancelRequested.load(std::memory_order_relaxed)) {
        state->status.store(ProcessStatus::Cancelled, std::memory_order_release);
      } else {
        state->status.store(ProcessStatus::Completed, std::memory_order_release);
      }
      return true;
    }

    return false;  // продовжуємо наступного разу
  };

  Process::registerAppPump(std::move(pump));
  return ProcessHandle<TProgress>(state);
}
