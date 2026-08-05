#include "SdStaticSource.hpp"

#include <ESPAsyncWebServer.h>

bool SdStaticSource::exists(const String& path) const { return _fs.exists(path); }

void SdStaticSource::handleRequest(AsyncWebServerRequest* request, const String& path) {
  request->send(_fs, path);
}
