#pragma once

// Реєстр "куди ще дублювати лог", прив'язаний до ПОТОЧНОГО таска.
//
// Навіщо: команда, що прийшла по MQTT, виконується через
// SerialCommander::execute(), а її результат десятки хендлерів пишуть у власні
// TLogger - повернути його викликачу нема як (CommandCallback повертає void).
// Замість переписування всіх хендлерів SerialLogger::log() дублює вже
// сформований рядок у sink, зареєстрований для таска, в якому виконується
// команда. Так у відповідь потрапляє і вивід бібліотечних логерів
// (MqttClient, SDCardInspector, EspPartitionInspector), який хендлер не
// контролює.
//
// Чому саме за таском, а не глобальним прапорцем: паралельно логують мережевий
// таск MqttClient ("mqtt-net") і "ecoflow-rest" - їхні рядки не мають
// потрапляти у відповідь на чужу команду.
//
// Напряму зазвичай не потрібен - див. ScopedLogCapture.hpp (RAII).

#include <Print.h>

#include <cstddef>

#if defined(ESP32)
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#endif

class LogCaptureRegistry {
public:
  static LogCaptureRegistry& instance();

  // Ставить sink для поточного таска і повертає той, що стояв до цього
  // (nullptr, якщо не було). Повернене значення потрібне для вкладеності:
  // ScopedLogCapture відновлює його в деструкторі. sink == nullptr - зняти
  // захоплення (використовується як re-entrancy guard на час доставки).
  Print* swap(Print* sink);

  // Sink поточного таска або nullptr.
  Print* current() const;

private:
  LogCaptureRegistry();

#if defined(ESP32)
  // Fixed-size масив замість map: без heap, як PrintQueue - щоб логер не
  // залежав від фрагментації купи. Одночасних захоплень у проєкті одиниці
  // (MQTT-команда, cron-задача), 4 слоти з великим запасом.
  static constexpr size_t kMaxSlots = 4;

  struct Slot {
    TaskHandle_t task = nullptr;
    Print* sink = nullptr;
  };

  Slot _slots[kMaxSlots] = {};
  SemaphoreHandle_t _mutex;
  StaticSemaphore_t _mutexBuffer;
#else
  // ESP8266: немає RTOS, немає конкурентного доступу в кооперативному loop()
  // (та сама логіка, що в PrintQueue.cpp) - вистачає одного слота без мьютекса.
  Print* _sink = nullptr;
#endif
};
