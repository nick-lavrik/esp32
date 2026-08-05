// Внутрішній singleton-реєстр PrintQueue за адресою Print&-виходу.
// Не використовувати напряму з коду логера - тільки через
// PrintQueue::flush() (дренаж) і PrintQueueRegistry::forOutput()
// (SerialLogger сам звертається за конкретною чергою).
//
// Повноцінний реєстр (unordered_map) існує лише на ESP32. На ESP8266
// PrintQueue стейтлесс (див. PrintQueue.cpp) - реєструвати нічого,
// forOutput() повертає єдиний статичний інстанс без heap/map.
#pragma once

#include <Print.h>

#include "PrintQueue.hpp"

#if defined(ESP32)
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <memory>
#include <unordered_map>
#endif

class PrintQueueRegistry {
public:
  static PrintQueueRegistry& instance();

  // Повертає чергу для output, створюючи її при першому зверненні.
  PrintQueue& forOutput(Print& output);

  // Дренажить усі зареєстровані черги.
  void flushAll();

private:
  PrintQueueRegistry();

#if defined(ESP32)
  SemaphoreHandle_t _mapMutex;
  StaticSemaphore_t _mapMutexBuffer;
  std::unordered_map<void*, std::unique_ptr<PrintQueue>> _queues;
#endif
};
