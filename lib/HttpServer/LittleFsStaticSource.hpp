#pragma once

#include <FS.h>

#include "IStaticSource.hpp"

// Віддає файли з LittleFS. contentType визначається автоматично
// всередині ESPAsyncWebServer за розширенням шляху (AsyncFileResponse
// сам мапить .html/.css/.js/тощо), тому тут не дублюємо mime-таблицю.
//
// basePath (опціонально) - каталог, у якому лежить статика: запит "/app.js"
// читається з "<basePath>/app.js". Потрібен, бо корінь LittleFS у цьому
// проєкті вже зайнятий іншим вмістом (фонові JPEG, /network/*.nmconnection),
// і вебка тримається окремо в "/www".
//
// Приклад:
//   LittleFsStaticSource source(LittleFS, "/www");
//   compositeSource.addSource(&source);
class LittleFsStaticSource : public IStaticSource {
public:
  explicit LittleFsStaticSource(fs::FS& fs, const String& basePath = String())
      : _fs(fs), _basePath(basePath) {}

  bool exists(const String& path) const override;
  void handleRequest(AsyncWebServerRequest* request, const String& path) override;

private:
  // Шлях запиту -> шлях у файловій системі.
  String _resolve(const String& path) const { return _basePath + path; }

  fs::FS& _fs;
  String _basePath;
};
