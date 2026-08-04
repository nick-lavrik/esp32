#include "RwLockState.hpp"

RwLockState::RwLockState() {
  stateMutex = xSemaphoreCreateMutexStatic(&stateMutexBuffer);
  event = xEventGroupCreateStatic(&eventBuffer);
}
