#include "WebEcoflowModule.hpp"

#include <WebJson.hpp>
#include <WebPortal.hpp>

namespace {

// -1/NAN - сентинели "ще не приходило" (EcoflowDeviceRegistry.hpp) - у JSON
// це null, а не -1: клієнт інакше намалював би "-1 W" замість прочерку.
String intOrNull(int32_t value) { return value < 0 ? String("null") : String(value); }

// Той самий трьохстановий статус, що в серійній команді 'ecoflow' (main.cpp):
// lastMessageMs == 0 означає "жодного повідомлення ще не чули", і це не
// синонім "offline" - REST-знімок може заповнити SOC/GRID ще до першого MQTT.
const char* presenceOf(const EcoflowDeviceState& state) {
  if (state.lastMessageMs == 0) return "unknown";
  return state.online ? "online" : "offline";
}

String deviceJson(const EcoflowDeviceState& state) {
  const uint32_t now = millis();
  String json = "{\"serialNumber\":" + webjson::quote(state.info->serialNumber);
  json += ",\"name\":" + webjson::quote(state.info->name);
  json += ",\"type\":" + webjson::quote(ecoflowDeviceTypeName(state.info->type));
  json += ",\"presence\":" + webjson::quote(presenceOf(state));
  json += ",\"online\":" + webjson::boolean(state.online);
  json += ",\"messageCount\":" + String(state.messageCount);
  json += ",\"ageMs\":" + (state.lastMessageMs == 0 ? String("null") : String(now - state.lastMessageMs));
  json += ",\"lastMessageEpoch\":" + String((uint32_t)state.lastMessageEpoch);

  json += ",\"socPercent\":" + (state.hasSoc() ? String((int)state.socPercent) : String("null"));
  json += ",\"socPrecise\":" + (isnan(state.socPrecise) ? String("null") : String(state.socPrecise, 1));

  json += ",\"grid\":" + webjson::quote(ecoflowGridStateName(state.grid));
  json += ",\"gridInferred\":" + webjson::boolean(state.gridInferred);
  json += ",\"gridForMs\":" +
          (state.gridSinceMs == 0 ? String("null") : String(now - state.gridSinceMs));
  json += ",\"previousGrid\":" + webjson::quote(ecoflowGridStateName(state.previousGrid));
  json += ",\"previousGridDurationMs\":" + String(state.previousGridDurationMs);
  json += ",\"gridChangeCount\":" + String(state.gridChangeCount);

  json += ",\"acInputMilliVolts\":" + intOrNull(state.acInputMilliVolts);
  json += ",\"acInputFrequency\":" + intOrNull(state.acInputFrequency);
  json += ",\"inputWatts\":" + intOrNull(state.inputWatts);
  json += ",\"outputWatts\":" + intOrNull(state.outputWatts);
  json += ",\"remainTimeMinutes\":" + intOrNull(state.remainTimeMinutes);

  json += ",\"snapshotAvailable\":" + webjson::boolean(state.snapshotAvailable);
  json += ",\"captureAll\":" + webjson::boolean(state.captureAll);
  json += ",\"droppedParams\":" + String(state.droppedParams);

  // Сирі поля з quota/REST-знімка - усе, що реально прийшло понад іменовані
  // поля вище. Ключі вже нормалізовані (EcoflowDeviceRegistry::normalizeKey),
  // тому валідні як JSON-ключі без екранування.
  json += ",\"params\":{";
  bool first = true;
  for (const auto& kv : state.trackedParams) {
    if (!first) json += ',';
    first = false;
    json += webjson::quote(kv.first.c_str());
    json += ':';
    json += String(kv.second, 3);
  }
  json += "}}";
  return json;
}

}  // namespace

void WebEcoflowModule::registerRoutes(AsyncWebServer& server, WebPortal& portal) {
  (void)portal;

  server.on("/api/ecoflow/status", HTTP_GET, [this](AsyncWebServerRequest* request) {
    String json = "{\"connected\":" + webjson::boolean(_client.isConnected());
    json += ",\"running\":" + webjson::boolean(_client.isRunning());
    json += ",\"channel\":" + webjson::quote(EcoflowClient::channelName(_client.channel()));
    json += ",\"account\":" + webjson::quote(_client.account());
    json += ",\"brokerHost\":" + webjson::quote(_client.brokerHost() ? _client.brokerHost() : "");
    json += ",\"brokerPort\":" + String(_client.brokerPort());
    json += ",\"viaProxy\":" + webjson::boolean(_client.viaProxy());
    json += ",\"messageCount\":" + String(_client.messageCount());
    json += ",\"lastTopic\":" + webjson::quote(_client.lastTopic());
    json += ",\"lastError\":" + webjson::quote(_client.lastError());
    json += ",\"heapFreeBytes\":" + String((uint32_t)ESP.getFreeHeap());
    json += ",\"heapLargestBlockBytes\":" + String((uint32_t)ESP.getMaxAllocHeap());
    json += ",\"netStackHeadroomBytes\":" + String((uint32_t)_client.networkStackHeadroom());

    json += ",\"devices\":[";
    bool first = true;
    for (const auto& state : _registry.devices()) {
      if (!first) json += ',';
      first = false;
      json += deviceJson(state);
    }
    json += "]}";

    request->send(200, "application/json", json);
  });
}
