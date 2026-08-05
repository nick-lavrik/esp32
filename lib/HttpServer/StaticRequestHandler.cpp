#include "StaticRequestHandler.hpp"

#include <Arduino.h>

String StaticRequestHandler::_resolvePath(const String& url) const {
  return (url == "/") ? _indexPath : url;
}

bool StaticRequestHandler::canHandle(AsyncWebServerRequest* request) const {
  // TODO(debug): прибрати після діагностики.
  Serial.printf("[StaticRequestHandler] canHandle: method=%u url='%s' staticSource=%p\n",
                (unsigned)request->method(), request->url().c_str(), (void*)_staticSource);

  if (_staticSource == nullptr || request->method() != HTTP_GET) {
    Serial.println("[StaticRequestHandler] canHandle: false (nullptr or not GET)");
    return false;
  }

  String resolved = _resolvePath(request->url());
  bool found = _staticSource->exists(resolved);
  Serial.printf("[StaticRequestHandler] canHandle: resolved='%s' exists=%s\n", resolved.c_str(),
                found ? "true" : "false");

  return found;
}

void StaticRequestHandler::handleRequest(AsyncWebServerRequest* request) {
  if (_staticSource == nullptr) {
    request->send(404);
    return;
  }

  _staticSource->handleRequest(request, _resolvePath(request->url()));
}
