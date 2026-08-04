// Внутрішній стан одного зареєстрованого через rwlock об'єкта.
// Не використовувати напряму — тільки через RwLock.hpp / namespace rwlock.
#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/semphr.h>

struct RwLockState {
  // Єдиний "changed" біт — сигналізує будь-яку зміну стану (unlock/timeout),
  // усі очікувачі (reader і writer) прокидаються і самі перевіряють свою умову
  // під stateMutex. Класична емуляція condition variable через event group.
  static constexpr EventBits_t kChangedBit = BIT0;

  RwLockState();

  StaticSemaphore_t stateMutexBuffer;
  SemaphoreHandle_t stateMutex;
  StaticEventGroup_t eventBuffer;
  EventGroupHandle_t event;

  int activeReaders = 0;
  bool activeWriter = false;
  int waitingWriters = 0;
};
