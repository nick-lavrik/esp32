#include "PrintQueue.hpp"

#include <RwLock.hpp>
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
        bool sent = rwlock::write(_output, /*timeoutMs=*/0,
                                   [this]() { _output.print(_lines[_head]); });
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

void PrintQueue::flush() {
    PrintQueueRegistry::instance().flushAll();
}
