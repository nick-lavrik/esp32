#pragma once

// Черга рядків, що не вдалось одразу записати в конкретний Print-вихід
// (напр. Serial зайнятий іншим потоком/callback-ом). Fixed-size ring
// buffer, без heap для самих рядків - навмисно, щоб не залежати від
// фрагментації купи на платах без PSRAM (esp8266).
//
// Прив'язана до одного Print&-виходу і дістається через
// PrintQueueRegistry::forOutput(output) за адресою - тому кілька
// TLogger з різними тегами, що пишуть в один Serial, діляться одною
// чергою і зберігають порядок повідомлень між тегами. Не створювати
// напряму - тільки через PrintQueueRegistry.
//
// Використання (напряму зазвичай не потрібно - див. SerialLogger::log()):
//   PrintQueue& q = PrintQueueRegistry::instance().forOutput(Serial);
//   q.tryWrite(line, /*timeoutMs=*/10);
//   ...
//   // в loop() кожної плати - дренажить усі зареєстровані черги:
//   PrintQueue::flush();
//
// Розмір черги (кількість рядків) - через build_flags, напр.:
//   build_flags = -D DEFAULT_PRINT_QUEUE_SIZE=10
// esp8266 (мало RAM) лишає дефолт (5) або зменшує, плати з PSRAM
// можуть збільшити.

#include <Print.h>
#include <cstddef>
#include <cstdint>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#ifndef DEFAULT_PRINT_QUEUE_SIZE
#define DEFAULT_PRINT_QUEUE_SIZE 5
#endif

class PrintQueue {
public:
    static constexpr size_t kCapacity = DEFAULT_PRINT_QUEUE_SIZE;
    static constexpr size_t kLineSize = 160;

    explicit PrintQueue(Print& output);

    // Спершу неблокуюче дренажить те, що вже в черзі (зберігає порядок),
    // потім пробує записати line напряму з таймаутом timeoutMs. Якщо
    // пряме write не вдалось (output зайнятий) - кладе line у чергу
    // (drop-oldest при переповненні). Повертає true, якщо line пішов
    // напряму в output без буферизації.
    bool tryWrite(const char* line, uint32_t timeoutMs);

    // Неблокуюча спроба виштовхати накопичене. Викликати періодично,
    // якщо після невдалого tryWrite() довго не буде нових log()-викликів
    // (інакше чергу продренажить лише наступний tryWrite()).
    void drain();

    // Дренажить усі зареєстровані черги (усі Print-виходи). Викликати
    // з loop() кожної плати.
    static void flush();

private:
    void pushLocked(const char* line);
    void drainLocked();

    Print& _output;
    SemaphoreHandle_t _mutex;
    StaticSemaphore_t _mutexBuffer;

    char _lines[kCapacity][kLineSize] = {};
    size_t _head = 0;   // індекс найстарішого елемента
    size_t _count = 0;  // скільки елементів фактично в черзі
};
