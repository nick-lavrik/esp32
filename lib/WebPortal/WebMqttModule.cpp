#include "WebMqttModule.hpp"

#include <WebJson.hpp>
#include <WebPortal.hpp>

void WebMqttModule::registerRoutes(AsyncWebServer& server, WebPortal& portal) {
  (void)portal;

  server.on("/api/mqtt/status", HTTP_GET, [this](AsyncWebServerRequest* request) {
    String json = "{\"connected\":" + webjson::boolean(_client.isConnected());
    json += ",\"host\":" + webjson::quote(_client.host() ? _client.host() : "");
    json += ",\"port\":" + String(_client.port());
    json += ",\"security\":" + webjson::quote(_client.usesTls() ? "tls" : "plain");
    json += ",\"clientId\":" + webjson::quote(_client.clientId() ? _client.clientId() : "");
    json += ",\"topicPrefix\":" + webjson::quote(_client.keyGenerator().prefix());
    json += ",\"publishedCount\":" + String(_client.publishedCount());
    json += ",\"receivedCount\":" + String(_client.receivedCount());
    json += ",\"subscribeDeniedCount\":" + String(_client.subscribeDeniedCount());
    json += ",\"netStackHeadroomBytes\":" + String((uint32_t)_client.networkTaskStackHeadroom());

    json += ",\"consoleMirror\":";
#if HAS_CONSOLE_MQTT
    json += "{\"available\":true";
    json += ",\"active\":" + webjson::boolean(_consoleMqtt->active());
    json += ",\"topic\":" + webjson::quote(_consoleMqtt->topic());
    json += ",\"publishedCount\":" + String(_consoleMqtt->publishedCount());
    json += ",\"droppedByRateLimitCount\":" + String(_consoleMqtt->droppedByRateLimitCount());

    json += ",\"allowRules\":[";
    const auto allow = _consoleMqtt->allowRules();
    for (size_t i = 0; i < allow.size(); ++i) {
      if (i > 0) json += ',';
      json += webjson::quote(allow[i]);
    }
    json += "]";

    json += ",\"denyRules\":[";
    const auto deny = _consoleMqtt->denyRules();
    for (size_t i = 0; i < deny.size(); ++i) {
      if (i > 0) json += ',';
      json += webjson::quote(deny[i]);
    }
    json += "]";
    json += "}";
#else
    json += "{\"available\":false}";
#endif
    json += "}";

    request->send(200, "application/json", json);
  });
}
