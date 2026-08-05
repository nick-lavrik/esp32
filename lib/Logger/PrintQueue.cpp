#include "PrintQueue.hpp"

#include <RwLock.hpp>

// Порядок гілок навмисно починається з ESP8266 - той самий підхід і
// обгрунтування, що й у RwLock.cpp: ESP8266 - найбільш обмежена по RAM
// плата і без FreeRTOS, тож для неї природньо мати найпростішу гілку
// першою. #else #error - явний allow-list підтримуваних платформ.
#if defined(ESP8266)

// --- ESP8266: NONOS SDK, немає FreeRTOS ---------------------------------
// rwlock::write() на цій платформі - always-success no-op (кооперативний
// однопотоковий loop(), реальної конкуренції за _output нема - див.
// RwLock.cpp). Тобто пряме write ніколи "не вдається", і черга нічого не
// зробила б, крім витрачання RAM. PrintQueue тут - тонка обгортка без
// мьютексів, без ring buffer, без heap-алокацій.

PrintQueue::PrintQueue(Print& output) : _output(output) {}

bool PrintQueue::tryWrite(const char* line, uint32_t timeoutMs) {
  return rwlock::write(_output, timeoutMs, [this, line]() { _output.print(line); });
}

void PrintQueue::drain() {
  // немає що дренажити - tryWrite() завжди виконується напряму
}

void PrintQueue::flush() {
  // немає зареєстрованих черг (PrintQueueRegistry на ESP8266 теж no-op)
}

#elif defined(ESP32)

#include <cstring>

#include "PrintQueueRegistry.hpp"

PrintQueue::PrintQueue(Print& output) : _output(output) {
  _mutex = xSemaphoreCreateMutexStatic(&_mutexBuffer);
}

bool PrintQueue::tryWrite(const char* line, uint32_t timeoutMs) {
  xSemaphoreTake(_mutex, portMAX_DELAY);

  drainLocked();  // спершу допровадити накопичене - зберігає порядок

  bool sentDirectly = false;
  if (_count == 0) {
    sentDirectly = rwlock::write(_output, timeoutMs, [this, line]() { _output.print(line); });
  }
  if (!sentDirectly) {
    pushLocked(line);
  }

  xSemaphoreGive(_mutex);
  return sentDirectly;
}

void PrintQueue::drain() {
  xSemaphoreTake(_mutex, portMAX_DELAY);
  drainLocked();
  xSemaphoreGive(_mutex);
}

void PrintQueue::drainLocked() {
  while (_count > 0) {
    bool sent = rwlock::write(_output, /*timeoutMs=*/0, [this]() { _output.print(_lines[_head]); });
    if (!sent) {
      break;
    }
    _head = (_head + 1) % kCapacity;
    --_count;
  }
}

void PrintQueue::pushLocked(const char* line) {
  size_t writeIndex;
  if (_count < kCapacity) {
    writeIndex = (_head + _count) % kCapacity;
    ++_count;
  } else {
    writeIndex = _head;  // drop-oldest
    _head = (_head + 1) % kCapacity;
  }
  strncpy(_lines[writeIndex], line, kLineSize - 1);
  _lines[writeIndex][kLineSize - 1] = '\0';
}

void PrintQueue::flush() { PrintQueueRegistry::instance().flushAll(); }

#else
#error "Unsupported platform: PrintQueue requires ESP8266 or ESP32"
#endif
