#pragma once

#include <Arduino.h>
#include "esp_heap_caps.h"

/**
 * HeapMonitor
 *
 * Періодично (не блокуюче, на базі millis()) логує стан пам'яті ESP32
 * через Serial: default heap, internal RAM, PSRAM (якщо є).
 *
 * Використання:
 *   HeapMonitor heapMonitor(5000); // кожні 5 секунд
 *
 *   void setup() {
 *       Serial.begin(115200);
 *       heapMonitor.begin();
 *   }
 *
 *   void loop() {
 *       heapMonitor.update(); // викликати в loop(), сам вирішить коли логувати
 *   }
 */
class HeapMonitor {
public:
    // intervalMs - як часто друкувати статистику (мілісекунди)
    explicit HeapMonitor(uint32_t intervalMs = 10000);

    // Викликати один раз у setup(). Друкує стан пам'яті одразу.
    void begin();

    // Викликати в loop(). Неблокуюче - друкує лише коли настав інтервал.
    void update();

    // Примусово надрукувати статистику зараз (ігноруючи інтервал)
    void printNow();

    // Змінити інтервал логування "на льоту"
    void setInterval(uint32_t intervalMs);

    // Отримати останнє відоме значення вільної пам'яті (default caps), без друку
    size_t getFreeHeap() const;

    // Отримати найбільший вільний неперервний блок (default caps)
    size_t getLargestFreeBlock() const;

private:
    uint32_t _intervalMs;
    uint32_t _lastPrintMs;

    bool _psramAvailable() const;
};
