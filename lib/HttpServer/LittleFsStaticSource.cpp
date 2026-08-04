// #include <Arduino.h>
#include "LittleFsStaticSource.hpp"
#include <ESPAsyncWebServer.h>

bool LittleFsStaticSource::exists(const String& path) const
{
    return  _fs.exists(path);
    // Serial.printf("LittleFS::exists(%s) = %s\n", path, _exists ? "yes" : "no");
    // return _exists;
}

void LittleFsStaticSource::handleRequest(AsyncWebServerRequest* request, const String& path)
{
    request->send(_fs, path);
}
