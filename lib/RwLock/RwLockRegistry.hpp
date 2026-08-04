// Внутрішній singleton-реєстр. Не використовувати напряму — тільки через
// rwlock::registerObject() / rwlock::rlock() / rwlock::wlock().
#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <memory>
#include <unordered_map>

#include "RwLockState.hpp"

class RwLockRegistry {
 public:
  static RwLockRegistry& instance();

  void registerObject(void* obj);
  RwLockState* find(void* obj);  // nullptr, якщо obj не зареєстровано

 private:
  RwLockRegistry();

  SemaphoreHandle_t _mapMutex;
  StaticSemaphore_t _mapMutexBuffer;
  std::unordered_map<void*, std::unique_ptr<RwLockState>> _states;
};
