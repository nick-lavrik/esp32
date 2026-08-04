#include "RwLock.hpp"

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <cassert>

#include "RwLockRegistry.hpp"
#include "RwLockState.hpp"

namespace {

TickType_t deadlineTicks(uint32_t timeoutMs) {
  return xTaskGetTickCount() + pdMS_TO_TICKS(timeoutMs);
}

TickType_t remainingTicks(TickType_t deadline) {
  TickType_t now = xTaskGetTickCount();
  if (now >= deadline) return 0;
  return deadline - now;
}

}  // namespace

void rwlock::registerObject(void* obj) {
  RwLockRegistry::instance().registerObject(obj);
}

RwLockHandle rwlock::rlock(void* obj, uint32_t timeoutMs) {
  RwLockHandle handle;
  RwLockState* state = RwLockRegistry::instance().find(obj);
  assert(state != nullptr && "rwlock: object not registered, call rwlock::registerObject() first");
  if (state == nullptr) return handle;

  const TickType_t deadline = deadlineTicks(timeoutMs);

  while (true) {
    xSemaphoreTake(state->stateMutex, portMAX_DELAY);
    // writer-preference: новий reader не проходить, якщо є активний або
    // очікуючий writer.
    bool canProceed = !state->activeWriter && state->waitingWriters == 0;
    if (canProceed) state->activeReaders++;
    xSemaphoreGive(state->stateMutex);

    if (canProceed) {
      handle.target = obj;
      handle.isWriter = false;
      handle.valid = true;
      return handle;
    }

    TickType_t wait = remainingTicks(deadline);
    if (wait == 0) return handle;  // timeout, invalid handle

    xEventGroupWaitBits(state->event, RwLockState::kChangedBit, pdTRUE, pdFALSE, wait);
    // повертаємось на початок циклу і перечитуємо стан під мьютексом
  }
}

RwLockHandle rwlock::wlock(void* obj, uint32_t timeoutMs) {
  RwLockHandle handle;
  RwLockState* state = RwLockRegistry::instance().find(obj);
  assert(state != nullptr && "rwlock: object not registered, call rwlock::registerObject() first");
  if (state == nullptr) return handle;

  const TickType_t deadline = deadlineTicks(timeoutMs);

  xSemaphoreTake(state->stateMutex, portMAX_DELAY);
  state->waitingWriters++;
  xSemaphoreGive(state->stateMutex);

  while (true) {
    xSemaphoreTake(state->stateMutex, portMAX_DELAY);
    bool canProceed = !state->activeWriter && state->activeReaders == 0;
    if (canProceed) {
      state->activeWriter = true;
      state->waitingWriters--;
    }
    xSemaphoreGive(state->stateMutex);

    if (canProceed) {
      handle.target = obj;
      handle.isWriter = true;
      handle.valid = true;
      return handle;
    }

    TickType_t wait = remainingTicks(deadline);
    if (wait == 0) {
      // timeout: обов'язково прибрати себе з черги очікування, інакше
      // waitingWriters лишиться "застряглим" і заблокує всіх reader-ів назавжди.
      xSemaphoreTake(state->stateMutex, portMAX_DELAY);
      state->waitingWriters--;
      xSemaphoreGive(state->stateMutex);
      xEventGroupSetBits(state->event, RwLockState::kChangedBit);
      return handle;
    }

    xEventGroupWaitBits(state->event, RwLockState::kChangedBit, pdTRUE, pdFALSE, wait);
  }
}

void rwlock::unlock(RwLockHandle& handle) {
  if (!handle.valid) return;

  RwLockState* state = RwLockRegistry::instance().find(handle.target);
  assert(state != nullptr && "rwlock: unlock on unregistered object");

  if (state != nullptr) {
    xSemaphoreTake(state->stateMutex, portMAX_DELAY);
    if (handle.isWriter) {
      state->activeWriter = false;
    } else {
      state->activeReaders--;
    }
    xSemaphoreGive(state->stateMutex);
    xEventGroupSetBits(state->event, RwLockState::kChangedBit);
  }

  handle.valid = false;
  handle.target = nullptr;
}
