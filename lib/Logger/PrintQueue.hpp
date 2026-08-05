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
// Існує повноцінно лише на ESP32 (є FreeRTOS-мьютекс і сам ring buffer).
// На ESP8266 rwlock::write() - завжди-успішний no-op (немає RTOS, немає
// конкурентного доступу в кооперативному loop() - див. RwLock.cpp), тому
// пряме write ніколи "не вдається" і буферизувати нічого: PrintQueue там
// звужується до тонкої обгортки без стану (див. PrintQueue.cpp).
//
// Використання (напряму зазвичай не потрібно - див. SerialLogger::log()):
//   PrintQueue& q = PrintQueueRegistry::instance().forOutput(Serial);
//   q.tryWrite(line, /*timeoutMs=*/10);
//   ...
//   // в loop() кожної плати - дренажить усі зареєстровані черги:
//   PrintQueue::flush();
//
// Розмір черги на ESP32 (кількість рядків) - через build_flags, напр.:
//   build_flags = -D DEFAULT_PRINT_QUEUE_SIZE=10

#include <Print.h>
#include <cstddef>
#include <cstdint>

#if defined(ESP32)
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#endif

#ifndef DEFAULT_PRINT_QUEUE_SIZE
#define DEFAULT_PRINT_QUEUE_SIZE 5
#endif

class PrintQueue {
public:
    // kLineSize використовує SerialLogger для розміру свого стекового
    // буфера незалежно від платформи - лишається спільним.
    static constexpr size_t kLineSize = 160;

    explicit PrintQueue(Print& output);

    // Спершу неблокуюче дренажить те, що вже в черзі (зберігає порядок,
    // ESP32-only), потім пробує записати line напряму з таймаутом
    // timeoutMs. Якщо пряме write не вдалось - кладе line у чергу
    // (drop-oldest при переповненні, ESP32-only). Повертає true, якщо
    // line пішов напряму в output без буферизації. На ESP8266 - завжди
    // напряму, завжди true (див. коментар вище).
    bool tryWrite(const char* line, uint32_t timeoutMs);

    // Неблокуюча спроба виштовхати накопичене (ESP32). На ESP8266 - no-op.
    void drain();

    // Дренажить усі зареєстровані черги. Викликати з loop() кожної плати.
    static void flush();

private:
    Print& _output;

#if defined(ESP32)
    static constexpr size_t kCapacity = DEFAULT_PRINT_QUEUE_SIZE;

    void pushLocked(const char* line);
    void drainLocked();

    SemaphoreHandle_t _mutex;
    StaticSemaphore_t _mutexBuffer;

    char _lines[kCapacity][kLineSize] = {};
    size_t _head = 0;   // індекс найстарішого елемента
    size_t _count = 0;  // скільки елементів фактично в черзі
#endif
};
