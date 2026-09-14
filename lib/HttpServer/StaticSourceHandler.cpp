#include "StaticSourceHandler.hpp"

#include <Arduino.h>

String StaticSourceHandler::_resolvePath(const String& url) const {
  return (url == "/") ? _indexPath : url;
}

bool StaticSourceHandler::canHandle(AsyncWebServerRequest* request) const {
  if (_staticSource == nullptr || request->method() != HTTP_GET) return false;

  return _staticSource->exists(_resolvePath(request->url()));
}

void StaticSourceHandler::handleRequest(AsyncWebServerRequest* request) {
  if (_staticSource == nullptr) {
    request->send(404);
    return;
  }

  _staticSource->handleRequest(request, _resolvePath(request->url()));
}
