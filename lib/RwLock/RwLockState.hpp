// Внутрішній стан одного зареєстрованого через rwlock об'єкта.
// Не використовувати напряму — тільки через RwLock.hpp / namespace rwlock.
//
// Існує лише на ESP32: тут є FreeRTOS з preemptive-задачами, які реально
// можуть конкурувати за доступ до obj (наприклад кілька тасків пишуть у
// Serial). На ESP8266 немає RTOS (кооперативний однопотоковий loop()) —
// конкурентного доступу фізично нема, тому там rwlock — no-op без стану
// (див. RwLock.cpp), і цей файл на ESP8266 компілюється в пусте місце.
#pragma once

#if defined(ESP32)

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

#endif  // defined(ESP32)
