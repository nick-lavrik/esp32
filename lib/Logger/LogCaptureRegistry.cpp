#include "LogCaptureRegistry.hpp"

// Порядок гілок навмисно починається з ESP8266 - див. пояснення в
// RwLock.cpp / PrintQueue.cpp.
#if defined(ESP8266)

LogCaptureRegistry::LogCaptureRegistry() {}

LogCaptureRegistry& LogCaptureRegistry::instance() {
  static LogCaptureRegistry registry;
  return registry;
}

Print* LogCaptureRegistry::swap(Print* sink) {
  Print* previous = _sink;
  _sink = sink;
  return previous;
}

Print* LogCaptureRegistry::current() const { return _sink; }

#elif defined(ESP32)

LogCaptureRegistry::LogCaptureRegistry() { _mutex = xSemaphoreCreateMutexStatic(&_mutexBuffer); }

LogCaptureRegistry& LogCaptureRegistry::instance() {
  static LogCaptureRegistry registry;  // magic static - thread-safe одноразова ініціалізація
  return registry;
}

Print* LogCaptureRegistry::swap(Print* sink) {
  TaskHandle_t task = xTaskGetCurrentTaskHandle();

  xSemaphoreTake(_mutex, portMAX_DELAY);

  Slot* slot = nullptr;
  Slot* freeSlot = nullptr;
  for (size_t i = 0; i < kMaxSlots; ++i) {
    if (_slots[i].task == task) {
      slot = &_slots[i];
      break;
    }
    if (freeSlot == nullptr && _slots[i].task == nullptr) {
      freeSlot = &_slots[i];
    }
  }

  Print* previous = nullptr;

  if (slot != nullptr) {
    previous = slot->sink;
    slot->sink = sink;
    // Слот звільняємо, лише коли захоплення знято повністю - інакше в масиві
    // накопичувались би записи від тасків, які давно завершились.
    if (sink == nullptr) {
      slot->task = nullptr;
    }
  } else if (sink != nullptr && freeSlot != nullptr) {
    freeSlot->task = task;
    freeSlot->sink = sink;
  }
  // Слотів не лишилось - захоплення просто не відбудеться: лог і далі йде в
  // Serial, відповідь буде порожня. Мовчки, бо логувати звідси не можна:
  // SerialLogger::log() сам викликає current() і це дало б рекурсію.

  xSemaphoreGive(_mutex);

  return previous;
}

Print* LogCaptureRegistry::current() const {
  TaskHandle_t task = xTaskGetCurrentTaskHandle();

  // Читання без мьютекса: викликається з SerialLogger::log() на КОЖЕН рядок
  // логу, у т.ч. з мережевих тасків - брати мьютекс там означало б ставити
  // логер у залежність від планувальника. Гонка тут нешкідлива: слот пише
  // лише сам таск-власник (swap() під мьютексом), тож інший таск або не
  // знайде свого запису, або побачить консистентну пару task/sink. Чужий
  // напів-записаний слот не матчиться по task і пропускається.
  for (size_t i = 0; i < kMaxSlots; ++i) {
    if (_slots[i].task == task) {
      return _slots[i].sink;
    }
  }

  return nullptr;
}

#else
#error "Unsupported platform: LogCaptureRegistry requires ESP8266 or ESP32"
#endif
