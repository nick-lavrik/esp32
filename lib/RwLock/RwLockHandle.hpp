// Handle, що повертається rwlock::rlock()/wlock(). Перевіряти через `if (handle)`.
// Використання:
//   RwLockHandle h = rwlock::wlock(Serial, 10);
//   if (h) {
//     Serial.println("exclusive lock acquired");
//     rwlock::unlock(h);
//   } else {
//     // не вдалось отримати доступ за 10мс
//   }
#pragma once

struct RwLockHandle {
  void* target = nullptr;
  bool isWriter = false;
  bool valid = false;

  explicit operator bool() const { return valid; }
};
