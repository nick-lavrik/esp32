#include "RwLockRegistry.hpp"

// Тіло файлу існує лише на ESP32 — див. коментар у RwLockRegistry.hpp.
#if defined(ESP32)

RwLockRegistry::RwLockRegistry() { _mapMutex = xSemaphoreCreateMutexStatic(&_mapMutexBuffer); }

RwLockRegistry& RwLockRegistry::instance() {
  static RwLockRegistry registry;  // magic static — thread-safe одноразова ініціалізація
  return registry;
}

void RwLockRegistry::registerObject(void* obj) {
  if (obj == nullptr) return;

  xSemaphoreTake(_mapMutex, portMAX_DELAY);
  if (_states.find(obj) == _states.end()) {
    _states[obj] = std::make_unique<RwLockState>();
  }
  xSemaphoreGive(_mapMutex);
}

RwLockState* RwLockRegistry::find(void* obj) {
  xSemaphoreTake(_mapMutex, portMAX_DELAY);
  auto it = _states.find(obj);
  RwLockState* result = (it != _states.end()) ? it->second.get() : nullptr;
  xSemaphoreGive(_mapMutex);
  return result;
}

#endif  // defined(ESP32)
