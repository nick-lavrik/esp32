#include "PrintQueueRegistry.hpp"

// Порядок гілок навмисно починається з ESP8266 - див. пояснення в
// RwLock.cpp / PrintQueue.cpp.
#if defined(ESP8266)

// ESP8266: PrintQueue сам по собі стейтлесс на цій платформі (rwlock -
// always-success no-op, черга ніколи не потрібна). Тож реального
// реєстру за адресою тут немає: forOutput() повертає єдиний статичний
// інстанс, прив'язаний до output з ПЕРШОГО виклику (function-local
// static ініціалізується один раз). У цьому проєкті логер на ESP8266
// завжди пише в один Serial, тож це коректно для реального use-case.
// Якщо колись знадобиться кілька різних Print-виходів одночасно на
// ESP8266 - цю гілку треба переробити на невеликий fixed-size масив
// замість unordered_map (без heap), а не на копію ESP32-гілки.

PrintQueueRegistry::PrintQueueRegistry() {}

PrintQueueRegistry& PrintQueueRegistry::instance() {
    static PrintQueueRegistry registry;
    return registry;
}

PrintQueue& PrintQueueRegistry::forOutput(Print& output) {
    static PrintQueue single(output);
    return single;
}

void PrintQueueRegistry::flushAll() {
    // немає зареєстрованих черг - дренажити нічого
}

#elif defined(ESP32)

PrintQueueRegistry::PrintQueueRegistry() {
    _mapMutex = xSemaphoreCreateMutexStatic(&_mapMutexBuffer);
}

PrintQueueRegistry& PrintQueueRegistry::instance() {
    static PrintQueueRegistry registry;  // magic static - thread-safe одноразова ініціалізація
    return registry;
}

PrintQueue& PrintQueueRegistry::forOutput(Print& output) {
    void* key = static_cast<void*>(&output);

    xSemaphoreTake(_mapMutex, portMAX_DELAY);
    auto it = _queues.find(key);
    if (it == _queues.end()) {
        it = _queues.emplace(key, std::make_unique<PrintQueue>(output)).first;
    }
    PrintQueue& result = *it->second;
    xSemaphoreGive(_mapMutex);

    return result;
}

void PrintQueueRegistry::flushAll() {
    xSemaphoreTake(_mapMutex, portMAX_DELAY);
    for (auto& kv : _queues) {
        kv.second->drain();
    }
    xSemaphoreGive(_mapMutex);
}

#else
#error "Unsupported platform: PrintQueueRegistry requires ESP8266 or ESP32"
#endif
