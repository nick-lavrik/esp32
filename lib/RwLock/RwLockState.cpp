#include "RwLockState.hpp"

// Тіло файлу існує лише на ESP32 — див. коментар у RwLockState.hpp.
#if defined(ESP32)

RwLockState::RwLockState() {
  stateMutex = xSemaphoreCreateMutexStatic(&stateMutexBuffer);
  event = xEventGroupCreateStatic(&eventBuffer);
}

#endif  // defined(ESP32)
