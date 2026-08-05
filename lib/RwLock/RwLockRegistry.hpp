// Внутрішній singleton-реєстр. Не використовувати напряму — тільки через
// rwlock::registerObject() / rwlock::rlock() / rwlock::wlock().
//
// Існує лише на ESP32 — реєстр потрібен, щоб зіставити довільний obj з його
// RwLockState (RwLockState теж лише ESP32, див. коментар там). На ESP8266
// стану немає взагалі, тож і реєструвати нічого — файл компілюється в пусте
// місце.
#pragma once

#if defined(ESP32)

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

#endif  // defined(ESP32)
