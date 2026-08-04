// Reader-writer lock для довільних об'єктів (реєструються за адресою), writer-preference:
// якщо є writer, що чекає, нові reader-и блокуються (уникає writer starvation).
//
// Використання:
//   void setup() {
//     rwlock::registerObject(Serial);
//   }
//   ...
//   RwLockHandle h = rwlock::wlock(Serial, 10);  // timeoutMs
//   if (h) {
//     Serial.println("exclusive lock acquired");
//     rwlock::unlock(h);
//   } else {
//     // Serial exclusively acquired in another thread...
//   }
//
// Незареєстрований об'єкт: assert() у debug-збірці, no-op (invalid handle) у release.
#pragma once

#include <cstdint>
#include <functional>
#include "RwLockHandle.hpp"

namespace rwlock {

void registerObject(void* obj);

template <typename T>
void registerObject(T& obj) {
  registerObject(static_cast<void*>(&obj));
}

RwLockHandle rlock(void* obj, uint32_t timeoutMs);
RwLockHandle wlock(void* obj, uint32_t timeoutMs);

template <typename T>
RwLockHandle rlock(T& obj, uint32_t timeoutMs) {
  return rlock(static_cast<void*>(&obj), timeoutMs);
}

template <typename T>
RwLockHandle wlock(T& obj, uint32_t timeoutMs) {
  return wlock(static_cast<void*>(&obj), timeoutMs);
}

void unlock(RwLockHandle& handle);

using RwLockCallback = std::function<void()>;

template <typename T>
bool write(T& obj, uint32_t timeoutMs, RwLockCallback callback) {
  auto h = rwlock::wlock(obj, timeoutMs);
  if (h) {
    callback();
    rwlock::unlock(h);
    return true;
  }
  return false;
}

template <typename T>
bool read(T& obj, uint32_t timeoutMs, RwLockCallback callback) {
  auto h = rwlock::rlock(obj, timeoutMs);
  if (h) {
    callback();
    rwlock::unlock(h);
    return true;
  }
  return false;
}

}  // namespace rwlock
