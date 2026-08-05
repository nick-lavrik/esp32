#pragma once

#include <Event.hpp>

// Подія: HttpServer::end() зупинив AsyncWebServer.
// dispatch(event, HttpServerStoppedEvent::kEventName)
class HttpServerStoppedEvent : public Event {
public:
  static constexpr const char* kEventName = "HttpServer.stopped";
};
