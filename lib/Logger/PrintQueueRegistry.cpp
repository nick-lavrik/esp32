#include "PrintQueueRegistry.hpp"

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
