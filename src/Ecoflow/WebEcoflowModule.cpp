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

// Сирі "params" ОДНОГО пристрою у вигляді JSON-об'єкта {"ключ":число,...} -
// спільне ядро для includeParams-гілки deviceJson() нижче (portalStatusJson)
// і WebEcoflowModule::mqttDeviceParamsJson() (MQTT drill-down, main.cpp) -
// той самий цикл по trackedParams, не дві копії (DRY, CLAUDE.md).
String paramsObjectJson(const EcoflowDeviceState& state) {
  String json = "{";
  bool first = true;
  for (const auto& kv : state.trackedParams) {
    if (!first) json += ',';
    first = false;
    json += webjson::quote(kv.first.c_str());
    json += ':';
    json += String(kv.second, 3);
  }
  json += "}";
  return json;
}

// snap - провайдерський знімок (EcoflowDeviceState + похідні поля журналу,
// уже зняті в _refreshSnapshot()); nowMs/nowEpoch - момент ТОГО САМОГО
// знімка (WebEcoflowStatus::snapshotMs/snapshotEpoch), не живі millis()/
// time(nullptr) у форматері - інакше кожен пристрій в одній відповіді мав би
// трохи інший "нуль відліку". includeParams=false - для MQTT-форматера
// (mqttStatusJson нижче): "params" - єдине потенційно велике поле
// (captureAll -> до 353 записи на пристрій), решта - фіксованого розміру.
String deviceJson(const WebEcoflowDeviceSnapshot& snap, uint32_t nowMs, time_t nowEpoch, bool includeParams) {
  const EcoflowDeviceState& state = snap.state;
  String json = "{\"serialNumber\":" + webjson::quote(state.info->serialNumber);
  json += ",\"name\":" + webjson::quote(state.info->name);
  json += ",\"type\":" + webjson::quote(ecoflowDeviceTypeName(state.info->type));
  json += ",\"presence\":" + webjson::quote(presenceOf(state));
  json += ",\"online\":" + webjson::boolean(state.online);
  json += ",\"messageCount\":" + String(state.messageCount);
  json += ",\"ageMs\":" + (state.lastMessageMs == 0 ? String("null") : String(nowMs - state.lastMessageMs));
  json += ",\"lastMessageEpoch\":" + String((uint32_t)state.lastMessageEpoch);

  json += ",\"socPercent\":" + (state.hasSoc() ? String((int)state.socPercent) : String("null"));
  json += ",\"socPrecise\":" + (isnan(state.socPrecise) ? String("null") : String(state.socPrecise, 1));

  json += ",\"grid\":" + webjson::quote(ecoflowGridStateName(state.grid));
  json += ",\"gridInferred\":" + webjson::boolean(state.gridInferred);
  // "Скільки триває поточний стан" - з персистентного журналу
  // (EcoflowGridJournal::gridSinceEpoch()), не з RAM-only мітки: та
  // обнулялась би щоразу на ребуті (tech_debt.md, "не дублювати previousGrid").
  json += ",\"gridForMs\":" +
          (snap.gridSinceEpoch == 0 || nowEpoch < snap.gridSinceEpoch
               ? String("null")
               : String((uint32_t)(nowEpoch - snap.gridSinceEpoch) * 1000UL));
  // "Попередній стан"/"тривалість попереднього" тепер живуть лише в самому
  // журналі (кільце переходів) - показ одного останнього запису тут означав
  // би ще один транзитний NVS-запит на КОЖЕН пристрій КОЖНОГО опитування
  // цього роуту; лишено для майбутнього окремого /api/ecoflow/journal (план
  // журналу EcoFlow, docs/tech_debt.md розділ 8), а не тут.
  json += ",\"gridChangeCount\":" + String(snap.gridChangeCount);

  json += ",\"acInputMilliVolts\":" + intOrNull(state.acInputMilliVolts);
  json += ",\"acInputFrequency\":" + intOrNull(state.acInputFrequency);
  json += ",\"inputWatts\":" + intOrNull(state.inputWatts);
  json += ",\"outputWatts\":" + intOrNull(state.outputWatts);
  json += ",\"remainTimeMinutes\":" + intOrNull(state.remainTimeMinutes);

  json += ",\"snapshotAvailable\":" + webjson::boolean(state.snapshotAvailable);
  json += ",\"captureAll\":" + webjson::boolean(state.captureAll);
  json += ",\"droppedParams\":" + String(state.droppedParams);

  if (includeParams) {
    // Сирі поля з quota/REST-знімка - усе, що реально прийшло понад
    // іменовані поля вище. Ключі вже нормалізовані
    // (EcoflowDeviceRegistry::normalizeKey), тому валідні як JSON-ключі без
    // екранування.
    json += ",\"params\":" + paramsObjectJson(state);
  }
  json += "}";
  return json;
}

// Спільний хвіст двох публічних форматерів (portal/MQTT) - розрізняються
// лише прапорцем includeParams на кожен пристрій (deviceJson() вище).
String buildStatusJson(const WebEcoflowStatus& status, const std::vector<WebEcoflowDeviceSnapshot>& devices,
                       bool includeParams) {
  String json = "{\"connected\":" + webjson::boolean(status.connected);
  json += ",\"running\":" + webjson::boolean(status.running);
  json += ",\"channel\":" + webjson::quote(status.channel);
  json += ",\"account\":" + webjson::quote(status.account);
  json += ",\"brokerHost\":" + webjson::quote(status.brokerHost);
  json += ",\"brokerPort\":" + String(status.brokerPort);
  json += ",\"viaProxy\":" + webjson::boolean(status.viaProxy);
  json += ",\"messageCount\":" + String(status.messageCount);
  json += ",\"lastTopic\":" + webjson::quote(status.lastTopic);
  json += ",\"lastError\":" + webjson::quote(status.lastError);
  json += ",\"heapFreeBytes\":" + String(status.heapFreeBytes);
  json += ",\"heapLargestBlockBytes\":" + String(status.heapLargestBlockBytes);
  json += ",\"netStackHeadroomBytes\":" + String(status.netStackHeadroomBytes);

  json += ",\"devices\":[";
  bool first = true;
  for (const auto& snap : devices) {
    if (!first) json += ',';
    first = false;
    json += deviceJson(snap, status.snapshotMs, status.snapshotEpoch, includeParams);
  }
  json += "]}";
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
  WebEcoflowStatus status;
  status.connected = _client.isConnected();
  status.running = _client.isRunning();
  status.channel = EcoflowClient::channelName(_client.channel());
  status.account = _client.account();
  status.brokerHost = _client.brokerHost() ? _client.brokerHost() : "";
  status.brokerPort = _client.brokerPort();
  status.viaProxy = _client.viaProxy();
  status.messageCount = _client.messageCount();
  status.lastTopic = _client.lastTopic();
  status.lastError = _client.lastError();
  status.heapFreeBytes = (uint32_t)ESP.getFreeHeap();
  // ESP.getMaxAllocHeap() рахує MALLOC_CAP_INTERNAL - зокрема IRAM-регіони,
  // які String/JSON-буфер узагалі не може зайняти, тому число стояло на
  // місці (32756 Б) незалежно від реальної фрагментації. MALLOC_CAP_8BIT -
  // та сама формула, що й serial-команда 'heap' (main.cpp) - справді
  // байт-адресована пам'ять, придатна під String.
  status.heapLargestBlockBytes = (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
  status.netStackHeadroomBytes = (uint32_t)_client.networkStackHeadroom();
  status.snapshotMs = millis();
  status.snapshotEpoch = time(nullptr);

  // journal - "hot"-акцесори EcoflowGridJournal (RAM, без звернення до NVS).
  // nullptr не трапляється за нормальної роботи (кожен пристрій має свій
  // журнал, EcoflowDeviceRegistry::EcoflowDeviceRegistry()), але
  // перевіряємо, а не покладаємось.
  std::vector<WebEcoflowDeviceSnapshot> devices;
  devices.reserve(_registry.devices().size());
  for (const auto& state : _registry.devices()) {
    const EcoflowGridJournal* journal = _registry.journalAt(state.journalIndex);
    WebEcoflowDeviceSnapshot snap;
    snap.state = state;
    snap.gridSinceEpoch = journal != nullptr ? journal->gridSinceEpoch() : 0;
    snap.gridChangeCount = journal != nullptr ? journal->totalChangeCount() : 0;
    devices.push_back(std::move(snap));
  }

  Lock lock(_mutex);
  _status = std::move(status);
  _devices = std::move(devices);
}

String WebEcoflowModule::portalStatusJson(const WebEcoflowStatus& status,
                                           const std::vector<WebEcoflowDeviceSnapshot>& devices) {
  return buildStatusJson(status, devices, /*includeParams=*/true);
}

String WebEcoflowModule::mqttStatusJson(const WebEcoflowStatus& status,
                                         const std::vector<WebEcoflowDeviceSnapshot>& devices) {
  return buildStatusJson(status, devices, /*includeParams=*/false);
}

String WebEcoflowModule::mqttDeviceParamsJson(const WebEcoflowDeviceSnapshot& snap) {
  const EcoflowDeviceState& state = snap.state;
  String json = "{\"serialNumber\":" + webjson::quote(state.info->serialNumber);
  json += ",\"captureAll\":" + webjson::boolean(state.captureAll);
  json += ",\"droppedParams\":" + String(state.droppedParams);
  json += ",\"params\":" + paramsObjectJson(state);
  json += "}";
  return json;
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
    request->send(200, "application/json", portalStatusJson(statusSnapshot(), devicesSnapshot()));
  });
}
