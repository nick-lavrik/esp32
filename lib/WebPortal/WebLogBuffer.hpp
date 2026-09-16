#pragma once

// Кільцевий буфер останніх рядків логу для веб-консолі.
//
// Підписується в LogMirror, тобто бачить рядки З УСІХ тасків (як і
// MQTT-дзеркало, lib/ConsoleMqtt) - саме цього чекають від консолі: у вкладці
// браузера має бути те саме, що в serial-моніторі, включно з "mqtt-net" і
// "ecoflow-rest". LogMirror має кілька слотів, тож два дзеркала не витісняють
// одне одного.
//
// На відміну від ConsoleMqtt, тут буферизація Є і вона й є суттю: браузер
// опитує /api/console/log періодично, і рядки, що з'явились між опитуваннями,
// мусять дочекатись клієнта. Буфер fixed-size, без heap: kLines рядків по
// kLineSize байтів (за замовчуванням 60x160 = 9.6 КБ у .bss). Переповнення -
// drop-oldest, як у кільці Journal.
//
// Кожен рядок має наскрізний номер (seq). Клієнт надсилає ?since=<seq> і
// отримує тільки нове; якщо він відстав більше, ніж на розмір буфера, це
// видно з поля "from" у відповіді - пропуск можна показати явно, а не вдавати,
// що логів не було.
//
// Потокобезпека: write() кличуть з будь-якого таска (це робить SerialLogger),
// читання йде з таска сервера - обидва під одним мьютексом. Всередині write()
// нічого не логується, тож рекурсії немає.

#include <Arduino.h>
#include <Print.h>

#include <cstddef>

#if defined(ESP32)
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#endif

#ifndef WEB_CONSOLE_LINES
#define WEB_CONSOLE_LINES 60
#endif

class WebLogBuffer : public Print {
public:
  // Та сама межа, що в JournalEntry::kTextSize - рядок уже обрізаний логером,
  // довшим він сюди не приходить.
  static constexpr size_t kLineSize = 160;
  static constexpr size_t kLines = WEB_CONSOLE_LINES;

  WebLogBuffer();
  ~WebLogBuffer();

  WebLogBuffer(const WebLogBuffer&) = delete;
  WebLogBuffer& operator=(const WebLogBuffer&) = delete;

  // Print: SerialLogger віддає цілий рядок одним write(), але посимвольний
  // шлях теж підтримуємо - рядок збирається до '\n'.
  size_t write(uint8_t c) override;
  size_t write(const uint8_t* buffer, size_t size) override;

  // Номер, який отримає НАСТУПНИЙ рядок (тобто "кінець" потоку).
  uint32_t head() const;

  // Номер найстарішого рядка, що ще є в буфері.
  uint32_t tail() const;

  // Копіює рядок з номером seq у out. false - рядка вже (або ще) немає.
  bool get(uint32_t seq, char* out, size_t outSize) const;

  // Додає рядок від імені самої консолі (відлуння введеної команди).
  // Іде тільки у веб-буфер, у Serial не дублюється.
  void push(const char* line);

private:
  void _append(const char* line);

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

  char _lines[kLines][kLineSize] = {};
  uint32_t _head = 0;  // seq наступного рядка; водночас лічильник записаних

  // Недописаний рядок (посимвольний write або write() без '\n' у кінці).
  char _partial[kLineSize] = {};
  size_t _partialLength = 0;

#if defined(ESP32)
  SemaphoreHandle_t _mutex = nullptr;
#else
  int _mutex = 0;
#endif
};
