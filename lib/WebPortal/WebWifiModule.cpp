#include "WebWifiModule.hpp"

#include <ESPAsyncWebServer.h>

// Клас WiFi (WiFi.scanNetworks/encryptionType) приїздить сюди через
// NetworkSupervisor.hpp, який уже робить умовний інклюд по платформі -
// дублювати той #if тут не треба.

#include "WebJson.hpp"
#include "WebPortal.hpp"

namespace {

const char* stateName(NetworkSupervisorState state) {
  switch (state) {
    case NetworkSupervisorState::IDLE: return "idle";
    case NetworkSupervisorState::SCANNING: return "scanning";
    case NetworkSupervisorState::CONNECTING: return "connecting";
    case NetworkSupervisorState::CONNECTED: return "connected";
    case NetworkSupervisorState::RECONNECTING: return "reconnecting";
    case NetworkSupervisorState::WPS_WAITING: return "wps";
    case NetworkSupervisorState::STARTING_AP: return "starting-ap";
    case NetworkSupervisorState::AP_MODE: return "ap";
  }
  return "unknown";
}

}  // namespace

WebWifiModule::WebWifiModule(NetworkSupervisor& supervisor) : _supervisor(supervisor) {
#if defined(ESP32)
  _mutex = xSemaphoreCreateMutex();
#endif
}

WebWifiModule::~WebWifiModule() {
#if defined(ESP32)
  if (_mutex) vSemaphoreDelete(_mutex);
#endif
}

WifiConnection* WebWifiModule::_findBySsid(const String& ssid) {
  for (const auto& c : _supervisor.connections()) {
    if (ssid == c.ssid.c_str()) return _supervisor.getConnection(c.connectionId);
  }
  return nullptr;
}

void WebWifiModule::_refreshSnapshot() {
  const NetworkSupervisorConfig& cfg = _supervisor.config();
  const NetworkSupervisorState state = _supervisor.state();
  const bool apMode =
      state == NetworkSupervisorState::AP_MODE || state == NetworkSupervisorState::STARTING_AP;

  String status = "{\"state\":";
  status += webjson::quote(stateName(state));
  status += ",\"connected\":";
  status += webjson::boolean(_supervisor.isConnected());
  status += ",\"ssid\":";
  status += webjson::quote(_supervisor.currentSsid());
  status += ",\"ip\":";
  status += webjson::quote(_supervisor.localIp());
  status += ",\"rssi\":";
  status += _supervisor.isConnected() ? (int)WiFi.RSSI() : 0;
  // Відсоток рахує пристрій, а не сторінка: та сама шкала, що в 'status sys'
  // і на екрані плати (wifiSignalQuality() в NetworkSupervisor).
  status += ",\"quality\":";
  status += _supervisor.isConnected() ? wifiSignalQuality(WiFi.RSSI()) : 0;
  status += ",\"mac\":";
  status += webjson::quote(WiFi.macAddress());
  status += ",\"autoReconnect\":";
  status += webjson::boolean(_supervisor.autoReconnect());
  status += ",\"ap\":{\"active\":";
  status += webjson::boolean(apMode);
  status += ",\"ssid\":";
  status += webjson::quote(cfg.apSsid);
  status += ",\"ip\":";
  status += webjson::quote(apMode ? WiFi.softAPIP().toString() : String(cfg.apIp.c_str()));
  status += ",\"clients\":";
  status += apMode ? (int)WiFi.softAPgetStationNum() : 0;
  status += "}}";

  String connections = "[";
  bool first = true;
  for (const auto& c : _supervisor.connections()) {
    if (!first) connections += ',';
    first = false;

    connections += "{\"id\":";
    connections += c.connectionId;
    connections += ",\"ssid\":";
    connections += webjson::quote(c.ssid);
    // Пароль назовні не віддаємо - лише факт його наявності: сторінку
    // порталу може відкрити будь-хто, хто вже в мережі пристрою.
    connections += ",\"hasPassword\":";
    connections += webjson::boolean(!c.password.empty());
    connections += ",\"priority\":";
    connections += c.priority;
    connections += ",\"enabled\":";
    connections += webjson::boolean(c.isEnabled);
    connections += ",\"lastConnected\":";
    connections += c.lastConnected;
    connections += ",\"rssi\":";
    connections += c.rssi;
    connections += ",\"quality\":";
    connections += c.rssi != 0 ? wifiSignalQuality(c.rssi) : 0;
    // До якого профілю підключені ЗАРАЗ. Вирішує пристрій: він єдиний знає
    // і поточний SSID, і стан зʼєднання, а сторінці довелося б звіряти два
    // окремі запити й вгадувати, який із них свіжіший.
    connections += ",\"active\":";
    connections += webjson::boolean(_supervisor.isConnected() &&
                                    _supervisor.currentSsid() == c.ssid);
    connections += ",\"staticIp\":";
    connections += webjson::boolean(c.staticIp);
    connections += ",\"ip\":";
    connections += webjson::quote(c.ip);
    connections += ",\"gateway\":";
    connections += webjson::quote(c.gateway);
    connections += ",\"subnet\":";
    connections += webjson::quote(c.subnet);
    connections += ",\"dns\":";
    connections += webjson::quote(c.dns);
    connections += "}";
  }
  connections += "]";

  Lock lock(_mutex);
  _statusJson = std::move(status);
  _connectionsJson = std::move(connections);
}

String WebWifiModule::_scanJob() {
  // Під час власного скану FSM не має сканувати сам: два скани одночасно
  // дають WIFI_SCAN_FAILED (те саме застереження, що в NetworkSupervisor::scan()).
  const NetworkSupervisorState state = _supervisor.state();
  if (state == NetworkSupervisorState::SCANNING || state == NetworkSupervisorState::CONNECTING) {
    return webjson::fail("Supervisor is busy, try again in a moment");
  }

  // Точку доступу на час скану не гасимо (AP_STA): інакше клієнт, який сидить
  // на цій самій точці й натиснув "скан", втратив би з'єднання з порталом
  // рівно в момент відповіді.
  if (state == NetworkSupervisorState::AP_MODE) {
    WiFi.mode(WIFI_AP_STA);
  }

  const int16_t found = WiFi.scanNetworks(/*async=*/false, /*show_hidden=*/true);
  if (found < 0) {
    return webjson::fail("Scan failed");
  }

  const String activeSsid = WiFi.isConnected() ? WiFi.SSID() : String();

  String json = "{\"ok\":true,\"networks\":[";
  for (int16_t i = 0; i < found; ++i) {
    if (i > 0) json += ',';

    const String ssid = WiFi.SSID(i);
    json += "{\"ssid\":";
    json += webjson::quote(ssid);
    json += ",\"hidden\":";
    json += webjson::boolean(ssid.length() == 0);
    json += ",\"rssi\":";
    json += (int)WiFi.RSSI(i);
    json += ",\"quality\":";
    json += wifiSignalQuality(WiFi.RSSI(i));
    json += ",\"channel\":";
    json += (int)WiFi.channel(i);
    json += ",\"security\":";
    json += webjson::quote(wifiAuthTypeName((uint8_t)WiFi.encryptionType(i)));
    json += ",\"inUse\":";
    json += webjson::boolean(ssid.length() > 0 && ssid == activeSsid);
    json += ",\"known\":";
    json += webjson::boolean(ssid.length() > 0 && _findBySsid(ssid) != nullptr);
    json += "}";
  }
  json += "]}";

  WiFi.scanDelete();
  return json;
}

void WebWifiModule::loop() {
  const uint32_t now = millis();
  if (_lastSnapshotMs != 0 && (now - _lastSnapshotMs) < WEB_WIFI_SNAPSHOT_INTERVAL_MS) return;

  _lastSnapshotMs = now;
  _refreshSnapshot();
}

void WebWifiModule::registerRoutes(AsyncWebServer& server, WebPortal& portal) {
  // Перший знімок - одразу, щоб сторінка не побачила порожній "{}" у вікні
  // між begin() і першою ітерацією loop().
  _refreshSnapshot();
  _lastSnapshotMs = millis();

  // ---- читання ----
  server.on("/api/wifi/status", HTTP_GET, [this](AsyncWebServerRequest* request) {
    Lock lock(_mutex);
    request->send(200, "application/json", _statusJson);
  });

  server.on("/api/wifi/connections", HTTP_GET, [this](AsyncWebServerRequest* request) {
    Lock lock(_mutex);
    request->send(200, "application/json", _connectionsJson);
  });

  // ---- скан ефіру ----
  server.on("/api/wifi/scan", HTTP_POST, [this, &portal](AsyncWebServerRequest* request) {
    const uint32_t jobId = portal.jobs().submit([this]() { return _scanJob(); });
    if (jobId == 0) {
      request->send(503, "application/json", webjson::error("Job queue is full, try again"));
      return;
    }
    request->send(202, "application/json", String("{\"jobId\":") + jobId + "}");
  });

  // ---- зберегти/оновити профіль ----
  //
  // Семантика nmcli, як і в команді 'net device wifi connect': профіль
  // створюється, якщо його немає, і оновлюється, якщо він уже є.
  server.on("/api/wifi/connections", HTTP_POST, [this, &portal](AsyncWebServerRequest* request) {
    if (!request->hasParam("ssid", true)) {
      request->send(400, "application/json", webjson::error("Missing 'ssid' parameter"));
      return;
    }

    const String ssid = request->getParam("ssid", true)->value();
    const bool hasPassword = request->hasParam("password", true);
    const String password = hasPassword ? request->getParam("password", true)->value() : String();
    const bool hasPriority = request->hasParam("priority", true);
    const int priority =
        hasPriority ? request->getParam("priority", true)->value().toInt() : 0;
    const bool hasEnabled = request->hasParam("enabled", true);
    const bool enabled =
        hasEnabled ? request->getParam("enabled", true)->value() != "false" : true;
    const bool connectNow =
        request->hasParam("connect", true) && request->getParam("connect", true)->value() != "false";

    const uint32_t jobId = portal.jobs().submit(
        [this, ssid, password, hasPassword, priority, hasPriority, enabled, hasEnabled,
         connectNow]() -> String {
          WifiConnection* existing = _findBySsid(ssid);
          uint16_t id;

          if (existing != nullptr) {
            if (hasPassword) existing->password = password.c_str();
            if (hasPriority) existing->priority = (int8_t)priority;
            if (hasEnabled) existing->isEnabled = enabled;
            id = existing->connectionId;
          } else {
            WifiConnection conn;
            conn.ssid = ssid.c_str();
            conn.password = password.c_str();
            conn.priority = (int8_t)priority;
            conn.isEnabled = enabled;
            id = _supervisor.addConnection(conn);
          }

          _supervisor.saveConfig();

          if (!connectNow) return webjson::ok("Profile saved");

          // Явне підключення скасовує попередній ручний disconnect - інакше
          // FSM підключився б і лишився без нагляду (та сама логіка, що в
          // 'net device wifi connect').
          _supervisor.setAutoReconnect(true);
          if (!_supervisor.connectTo(id)) return webjson::fail("Profile vanished");
          return webjson::ok("Connecting");
        });

    if (jobId == 0) {
      request->send(503, "application/json", webjson::error("Job queue is full, try again"));
      return;
    }
    request->send(202, "application/json", String("{\"jobId\":") + jobId + "}");
  });

  // ---- видалити профіль ----
  server.on("/api/wifi/connections", HTTP_DELETE, [this, &portal](AsyncWebServerRequest* request) {
    if (!request->hasParam("id")) {
      request->send(400, "application/json", webjson::error("Missing 'id' parameter"));
      return;
    }

    const uint16_t id = (uint16_t)strtoul(request->getParam("id")->value().c_str(), nullptr, 10);
    const uint32_t jobId = portal.jobs().submit([this, id]() -> String {
      if (!_supervisor.removeConnection(id)) return webjson::fail("No such profile");
      _supervisor.saveConfig();
      return webjson::ok("Profile removed");
    });

    if (jobId == 0) {
      request->send(503, "application/json", webjson::error("Job queue is full, try again"));
      return;
    }
    request->send(202, "application/json", String("{\"jobId\":") + jobId + "}");
  });

  // ---- підключитись до збереженого профілю ----
  server.on("/api/wifi/connect", HTTP_POST, [this, &portal](AsyncWebServerRequest* request) {
    if (!request->hasParam("id", true)) {
      request->send(400, "application/json", webjson::error("Missing 'id' parameter"));
      return;
    }

    const uint16_t id = (uint16_t)strtoul(request->getParam("id", true)->value().c_str(), nullptr, 10);
    const uint32_t jobId = portal.jobs().submit([this, id]() -> String {
      _supervisor.setAutoReconnect(true);
      if (!_supervisor.connectTo(id)) return webjson::fail("No such profile");
      return webjson::ok("Connecting");
    });

    if (jobId == 0) {
      request->send(503, "application/json", webjson::error("Job queue is full, try again"));
      return;
    }
    request->send(202, "application/json", String("{\"jobId\":") + jobId + "}");
  });

  // ---- точка доступу ----
  server.on("/api/wifi/hotspot", HTTP_POST, [this, &portal](AsyncWebServerRequest* request) {
    const bool stop =
        request->hasParam("action", true) && request->getParam("action", true)->value() == "stop";

    const uint32_t jobId = portal.jobs().submit([this, stop]() -> String {
      if (stop) {
        _supervisor.stopAp();
        return webjson::ok("Hotspot stopped");
      }
      _supervisor.startAp();
      return webjson::ok("Hotspot started");
    });

    if (jobId == 0) {
      request->send(503, "application/json", webjson::error("Job queue is full, try again"));
      return;
    }
    request->send(202, "application/json", String("{\"jobId\":") + jobId + "}");
  });

  // ---- повний перебір мереж заново ----
  server.on("/api/wifi/reconnect", HTTP_POST, [this, &portal](AsyncWebServerRequest* request) {
    const uint32_t jobId = portal.jobs().submit([this]() -> String {
      _supervisor.setAutoReconnect(true);
      _supervisor.reconnect();
      return webjson::ok("Reconnecting");
    });

    if (jobId == 0) {
      request->send(503, "application/json", webjson::error("Job queue is full, try again"));
      return;
    }
    request->send(202, "application/json", String("{\"jobId\":") + jobId + "}");
  });
}
