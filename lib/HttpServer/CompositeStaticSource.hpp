#pragma once

#include <vector>
#include "IStaticSource.hpp"

// Пріоритетний ланцюжок джерел статики. Перше джерело в списку, для якого
// exists(path) == true, обробляє запит; решта не опитуються.
//
// Сам реалізує IStaticSource, тому композити можна вкладати один в одного
// (наприклад: "локальні джерела плати" -> вкладені в "спільний fallback
// набір" для кількох плат).
//
// Не володіє джерелами (не видаляє їх у деструкторі) — власник (main.cpp
// конкретної плати) відповідає за час життя переданих IStaticSource*.
class CompositeStaticSource : public IStaticSource
{
public:
    // priority: більше число - вищий пріоритет (перевіряється раніше).
    // При однаковому priority - порядок додавання (addSource раніше = раніше перевіряється).
    void addSource(IStaticSource* source, int priority = 0);

    bool exists(const String& path) const override;
    void handleRequest(AsyncWebServerRequest* request, const String& path) override;

private:
    struct Entry {
        IStaticSource* source;
        int priority;
    };

    std::vector<Entry> _sources;

    // Повертає перше джерело (за пріоритетом), яке має path, або nullptr.
    IStaticSource* _findSource(const String& path) const;
};
