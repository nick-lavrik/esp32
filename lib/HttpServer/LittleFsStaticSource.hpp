#pragma once

#include "IStaticSource.hpp"
#include <FS.h>

// Віддає файли з LittleFS. contentType визначається автоматично
// всередині ESPAsyncWebServer за розширенням шляху (AsyncFileResponse
// сам мапить .html/.css/.js/тощо), тому тут не дублюємо mime-таблицю.
//
// Приклад:
//   LittleFsStaticSource source(LittleFS);
//   compositeSource.addSource(&source);
class LittleFsStaticSource : public IStaticSource
{
public:
    explicit LittleFsStaticSource(fs::FS& fs) : _fs(fs) {}

    bool exists(const String& path) const override;
    void handleRequest(AsyncWebServerRequest* request, const String& path) override;

private:
    fs::FS& _fs;
};
