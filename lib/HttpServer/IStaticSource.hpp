#pragma once

#include <Arduino.h>

class AsyncWebServerRequest;

// Контракт джерела статичного контенту для WebServer.
//
// Кожна реалізація (LittleFsStaticSource, SdStaticSource,
// ProgmemStaticSource, CallbackStaticSource, ConfigStorageStaticSource)
// вирішує сама, як перевірити наявність шляху і як віддати відповідь.
// CompositeStaticSource дозволяє скласти кілька джерел у пріоритетний
// ланцюжок (наприклад SD -> LittleFS -> PROGMEM fallback) і сам також
// реалізує цей інтерфейс, тож ланцюжки можна вкладати один в інший.
class IStaticSource
{
public:
    virtual ~IStaticSource() = default;

    // Перевіряє, чи є в цьому джерелі контент за вказаним шляхом.
    // Викликається на кожен запит у StaticRequestHandler::canHandle(),
    // тому має бути дешевою операцією (без мережевих/довгих I/O викликів).
    virtual bool exists(const String& path) const = 0;

    // Формує та надсилає відповідь клієнту для вказаного шляху.
    // Викликається лише після exists(path) == true.
    virtual void handleRequest(AsyncWebServerRequest* request, const String& path) = 0;
};
