#pragma once

#include <cstdint>
#include <Event.hpp>

// Подія: HttpServer::begin() успішно підняв AsyncWebServer.
// dispatch(event, HttpServerStartedEvent::kEventName)
class HttpServerStartedEvent : public Event
{
public:
    static constexpr const char* kEventName = "HttpServer.started";

    HttpServerStartedEvent(uint16_t port, uint8_t maxClients)
        : _port(port), _maxClients(maxClients) {}

    uint16_t port() const { return _port; }
    uint8_t maxClients() const { return _maxClients; }

private:
    uint16_t _port;
    uint8_t _maxClients;
};
