#include "WebMqttModule.hpp"

#include <WebJson.hpp>
#include <WebPortal.hpp>

void WebMqttModule::registerRoutes(AsyncWebServer& server, WebPortal& portal) {
  (void)portal;

  server.on("/api/mqtt/status", HTTP_GET, [this](AsyncWebServerRequest* request) {
    request->send(200, "application/json", statusJson());
  });
}

String WebMqttModule::statusJson() const {
  String json = "{\"connected\":" + webjson::boolean(_client.isConnected());
  json += ",\"host\":" + webjson::quote(_client.host() ? _client.host() : "");
  json += ",\"port\":" + String(_client.port());
  json += ",\"security\":" + webjson::quote(_client.usesTls() ? "tls" : "plain");
  json += ",\"clientId\":" + webjson::quote(_client.clientId() ? _client.clientId() : "");
  // null - анонімний конект (username() сама вертає nullptr, коли
  // MqttConfig::username не задано - MqttClient::hasAuth()), а не порожній
  // рядок: інакше на сторінці виглядало б як логін-пусто, а не як "auth вимкнено".
  json += ",\"login\":" + (_client.username() ? webjson::quote(_client.username()) : String("null"));
  json += ",\"topicPrefix\":" + webjson::quote(_client.keyGenerator().prefix());
  json += ",\"publishedCount\":" + String(_client.publishedCount());
  json += ",\"receivedCount\":" + String(_client.receivedCount());
  json += ",\"subscribeDeniedCount\":" + String(_client.subscribeDeniedCount());
  // Ті самі втрати, що вже логуються через reportDroppedMessages() -
  // переповнення черги ДО відправки/прийому АБО провал алокації під копію
  // (catch(bad_alloc), lib/MqttClient/MqttClient.cpp). Причина не
  // розрізняється в самому лічильнику - CLAUDE.md, "Видимість MQTT-трафіку".
  json += ",\"droppedOutgoingCount\":" + String(_client.droppedOutgoingCount());
  json += ",\"droppedIncomingCount\":" + String(_client.droppedIncomingCount());
  json += ",\"netStackHeadroomBytes\":" + String((uint32_t)_client.networkTaskStackHeadroom());
  // CommandQueue::rejected() - черга команд (serial/web/MQTT/cron) уже
  // повна; кожне джерело саме каже про це вголос у моменті (CommandQueue.hpp),
  // але агрегат ніде не був видимий - той самий розрив, що dropped* вище.
  json += ",\"commandsRejectedCount\":" + String(_commandQueue.rejected());

  // LWT - null, якщо для цього клієнта не налаштовано (lwtTopic() поверне
  // nullptr/""). offline/online - опційні навіть коли topic заданий.
  const char* lwtTopic = _client.lwtTopic();
  if (lwtTopic != nullptr && lwtTopic[0] != '\0') {
    json += ",\"lwt\":{\"topic\":" + webjson::quote(lwtTopic);
    json += ",\"offlineMessage\":" + webjson::quote(_client.lwtOfflineMessage() ? _client.lwtOfflineMessage() : "");
    json += ",\"onlineMessage\":" + webjson::quote(_client.lwtOnlineMessage() ? _client.lwtOnlineMessage() : "");
    json += "}";
  } else {
    json += ",\"lwt\":null";
  }

  // Heartbeat - "доказ життя" в той самий LWT-топік між (пере)з'єднаннями,
  // окремо від offline/online. Значення - ті самі константи, що й у
  // реальному cron-таску (src/main.cpp) - див. коментар у WebMqttModule.hpp.
  json += ",\"heartbeat\":{\"message\":" + webjson::quote(_heartbeatMessage ? _heartbeatMessage : "");
  json += ",\"intervalMs\":" + String(_heartbeatIntervalMs) + "}";

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
  return json;
}
