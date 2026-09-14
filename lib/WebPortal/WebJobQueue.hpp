#pragma once

// Черга дій, які прийшли з HTTP, але ВИКОНУЮТЬСЯ в loop(), а не в таску
// AsyncTCP.
//
// Навіщо. Обробник ESPAsyncWebServer крутиться у власному таску сервера, і
// все, що він викликає, мусить бути коротким і неблокуючим. Майже все, що
// потрібно порталу, - протилежне:
//   - NetworkSupervisor::scan() блокує на 2-3 с (там власний цикл очікування
//     WiFi.scanComplete(), див. _runScan()), connectTo()/startAp() смикають
//     блокуючі WiFi.*;
//   - saveConfig() пише в NVS, тобто це flash I/O;
//   - serial-команда може робити будь-що - від друку сотень рядків до
//     звернення до шини дисплея, яку loop() тримає відкритою через кадр.
// Виконання цього в таску сервера вбило б сам сервер, а WiFi-виклики ще й
// ризикують тим самим дедлоком, який описаний у NetworkSupervisor.hpp
// (WiFi.* чекає на arduino event task, той упирається в замок).
//
// Тому HTTP-обробник лише КЛАДЕ замикання в чергу і одразу відповідає
// клієнту номером задачі; loop() виконує не більше однієї задачі за ітерацію;
// клієнт забирає результат окремим запитом за цим номером.
//
// Розміри фіксовані (без росту heap): kSlots задач у черзі, kResults
// останніх результатів. Коли результат витіснено - клієнт отримає статус
// "expired", а не тишу: інакше вкладка, що спізнилась із опитуванням,
// висіла б на "виконується" вічно.

#include <Arduino.h>

#include <functional>

#if defined(ESP32)
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#endif

#ifndef WEB_JOB_QUEUE_SLOTS
#define WEB_JOB_QUEUE_SLOTS 4
#endif

#ifndef WEB_JOB_RESULT_SLOTS
#define WEB_JOB_RESULT_SLOTS 4
#endif

class WebJobQueue {
public:
  // Повертає готовий результат (текст або JSON - вирішує сам модуль).
  using Handler = std::function<String()>;

  enum class Status : uint8_t {
    UNKNOWN,   // такої задачі не було, або її результат уже витіснено
    QUEUED,    // чекає на loop()
    RUNNING,   // виконується прямо зараз
    DONE,      // результат готовий
  };

  WebJobQueue();
  ~WebJobQueue();

  WebJobQueue(const WebJobQueue&) = delete;
  WebJobQueue& operator=(const WebJobQueue&) = delete;

  // Ставить задачу в чергу. Викликається з таска сервера.
  // Повертає id задачі або 0, якщо черга повна (клієнту - 503).
  uint32_t submit(Handler handler);

  // Стан задачі; при DONE заповнює outResult.
  Status status(uint32_t id, String& outResult) const;

  // Виконує НЕ БІЛЬШЕ однієї задачі. Викликати з loop().
  // Одна за ітерацію свідомо: дві підряд по 2-3 с кожна (скан ефіру) дали б
  // видимий провал кадру замість одного.
  void loop();

  // Скільки задач зараз чекає - для /api/status і діагностики.
  size_t pending() const;

private:
  struct Slot {
    uint32_t id = 0;
    Handler handler;
  };

  struct Result {
    uint32_t id = 0;
    String text;
  };

  static constexpr size_t kSlots = WEB_JOB_QUEUE_SLOTS;
  static constexpr size_t kResults = WEB_JOB_RESULT_SLOTS;

  // RAII-гард навколо _mutex; на ESP8266 вироджується в no-op (кооперативний
  // loop(), конкурентних тасків немає) - те саме рішення, що в Journal.
  class Lock {
  public:
#if defined(ESP32)
    explicit Lock(SemaphoreHandle_t m) : _m(m) {
      if (_m) xSemaphoreTake(_m, portMAX_DELAY);
    }
    ~Lock() {
      if (_m) xSemaphoreGive(_m);
    }

  private:
    SemaphoreHandle_t _m;
#else
    explicit Lock(int) {}
#endif
    Lock(const Lock&) = delete;
    Lock& operator=(const Lock&) = delete;
  };

  Slot _slots[kSlots];
  Result _results[kResults];
  size_t _nextResult = 0;
  uint32_t _nextId = 1;
  uint32_t _runningId = 0;

#if defined(ESP32)
  SemaphoreHandle_t _mutex = nullptr;
#else
  int _mutex = 0;
#endif
};
