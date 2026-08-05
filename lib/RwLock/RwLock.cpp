#include "RwLock.hpp"

// Порядок гілок навмисно починається з ESP8266 (а не з ESP32-варіанту "за
// замовчуванням"): ESP8266 у цьому проєкті — найбільш обмежена по RAM плата
// і водночас платформа БЕЗ FreeRTOS (NONOS SDK), тож для неї природньо
// й доцільно мати найпростішу гілку першою. #else #error — явний allow-list
// підтримуваних платформ: негативна перевірка на кшталт "#ifndef ESP32"
// мовчки "проходила б" на будь-якій новій/невідомій платформі, а тут це
// одразу помилка компіляції, яку неможливо пропустити.
#if defined(ESP8266)

// --- ESP8266: NONOS SDK, немає FreeRTOS ---------------------------------
// loop() виконується кооперативно в ОДНОМУ потоці: немає preemptive-задач,
// які могли б одночасно писати/читати один і той самий obj. Реальної
// конкуренції за доступ звідки б вона взялась нема — навіть SDK-callback'и
// (WiFi stack) не викликають Print/Serial напряму. Тому тут rwlock —
// свідома заглушка (always-success no-op): жодного реєстру, жодних
// мьютексів, жодних heap-алокацій (критично на платі з ~40-50KB вільного
// heap). Єдина мета цієї гілки — щоб спільний код (Logger тощо), що робить
// rwlock::wlock(_output, timeoutMs), компілювався й коректно поводився без
// #if на кожному місці виклику.

void rwlock::registerObject(void* obj) {
  (void)obj;  // немає стану, який реєструвати
}

RwLockHandle rwlock::rlock(void* obj, uint32_t timeoutMs) {
  (void)timeoutMs;  // немає конкуренції — таймаут ні на що не впливає
  RwLockHandle handle;
  handle.target = obj;
  handle.isWriter = false;
  handle.valid = true;
  return handle;
}

RwLockHandle rwlock::wlock(void* obj, uint32_t timeoutMs) {
  (void)timeoutMs;
  RwLockHandle handle;
  handle.target = obj;
  handle.isWriter = true;
  handle.valid = true;
  return handle;
}

void rwlock::unlock(RwLockHandle& handle) {
  handle.valid = false;
  handle.target = nullptr;
}

#elif defined(ESP32)

// --- ESP32: FreeRTOS, preemptive-задачі ---------------------------------
// Тут дійсно можливий конкурентний доступ з кількох FreeRTOS-тасків
// (наприклад ESPAsyncWebServer callback + основний loop() одночасно
// пишуть у Serial), тому потрібна повноцінна writer-preference
// реалізація на мьютексі + event group (емуляція condition variable).

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

void rwlock::registerObject(void* obj) { RwLockRegistry::instance().registerObject(obj); }

RwLockHandle rwlock::rlock(void* obj, uint32_t timeoutMs) {
  RwLockHandle handle;
  RwLockState* state = RwLockRegistry::instance().find(obj);
  assert(state != nullptr && "rwlock: object not registered, call rwlock::registerObject() first");
  if (state == nullptr) return handle;  // release: no-op, invalid handle

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
      // waitingWriters лишиться "застряглим" і заблокує всіх reader-ів
      // назавжди.
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

#else
#error "Unsupported platform: rwlock requires ESP8266 or ESP32"
#endif
