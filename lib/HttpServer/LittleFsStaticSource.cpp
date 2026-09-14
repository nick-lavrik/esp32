#include "LittleFsStaticSource.hpp"

#include <ESPAsyncWebServer.h>

bool LittleFsStaticSource::exists(const String& path) const { return _fs.exists(_resolve(path)); }

void LittleFsStaticSource::handleRequest(AsyncWebServerRequest* request, const String& path) {
  request->send(_fs, _resolve(path));
}
