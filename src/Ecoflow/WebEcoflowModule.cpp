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

// journal - "hot"-акцесори EcoflowGridJournal (RAM, без звернення до NVS,
// тому цей роут і далі не потребує WebJobQueue - див. коментар у
// WebEcoflowModule.hpp). nullptr не трапляється за нормальної роботи
// (кожен пристрій має свій журнал, EcoflowDeviceRegistry::EcoflowDeviceRegistry()),
// але перевіряємо, а не покладаємось.
String deviceJson(const EcoflowDeviceState& state, const EcoflowGridJournal* journal) {
  const uint32_t now = millis();
  const time_t sinceEpoch = journal != nullptr ? journal->gridSinceEpoch() : 0;
  const time_t nowEpoch = time(nullptr);
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
  // "Скільки триває поточний стан" - з персистентного журналу
  // (EcoflowGridJournal::gridSinceEpoch()), не з RAM-only мітки: та
  // обнулялась би щоразу на ребуті (tech_debt.md, "не дублювати previousGrid").
  json += ",\"gridForMs\":" +
          (sinceEpoch == 0 || nowEpoch < sinceEpoch ? String("null")
                                                    : String((uint32_t)(nowEpoch - sinceEpoch) * 1000UL));
  // "Попередній стан"/"тривалість попереднього" тепер живуть лише в самому
  // журналі (кільце переходів) - показ одного останнього запису тут означав
  // би ще один транзитний NVS-запит на КОЖЕН пристрій КОЖНОГО опитування
  // цього роуту; лишено для майбутнього окремого /api/ecoflow/journal (план
  // журналу EcoFlow, docs/tech_debt.md розділ 8), а не тут.
  json += ",\"gridChangeCount\":" + String(journal != nullptr ? journal->totalChangeCount() : 0);

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

WebEcoflowModule::WebEcoflowModule(EcoflowClient& client, EcoflowDeviceRegistry& registry)
    : _client(client), _registry(registry) {
#if defined(ESP32)
  _mutex = xSemaphoreCreateMutex();
#endif
}

WebEcoflowModule::~WebEcoflowModule() {
#if defined(ESP32)
  if (_mutex) vSemaphoreDelete(_mutex);
#endif
}

void WebEcoflowModule::_refreshSnapshot() {
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
    json += deviceJson(state, _registry.journalAt(state.journalIndex));
  }
  json += "]}";

  Lock lock(_mutex);
  _statusJson = std::move(json);
}

void WebEcoflowModule::loop() {
  const uint32_t now = millis();
  if (_lastSnapshotMs != 0 && now - _lastSnapshotMs < WEB_ECOFLOW_SNAPSHOT_INTERVAL_MS) return;
  _lastSnapshotMs = now;
  _refreshSnapshot();
}

void WebEcoflowModule::registerRoutes(AsyncWebServer& server, WebPortal& portal) {
  (void)portal;

  // Перший знімок - одразу, щоб сторінка не побачила порожній "{}" у вікні
  // між begin() і першою ітерацією loop() (той самий прийом, що WebWifiModule).
  _refreshSnapshot();
  _lastSnapshotMs = millis();

  server.on("/api/ecoflow/status", HTTP_GET, [this](AsyncWebServerRequest* request) {
    Lock lock(_mutex);
    request->send(200, "application/json", _statusJson);
  });
}
