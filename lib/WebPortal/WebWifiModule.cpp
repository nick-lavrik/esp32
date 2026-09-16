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

// Поля редактора профілю. Окрема структура, а не півтора десятка захоплень
// лямбди: майже кожне поле йде парою "значення + чи було воно в запиті" -
// відсутнє поле означає "не чіпати", а не "скинути в типове".
struct ProfileForm {
  uint16_t id = 0;  // 0 - новий профіль або пошук наявного за ssid
  String ssid;
  String password;
  bool hasPassword = false;
  int priority = 0;
  bool hasPriority = false;
  bool enabled = true;
  bool hasEnabled = false;
  int maxRetries = -1;
  bool hasMaxRetries = false;
  bool staticIp = false;
  bool hasStaticIp = false;
  String ip;
  String gateway;
  String subnet;
  String dns;
  bool connectNow = false;
};

String formParam(AsyncWebServerRequest* request, const char* name, bool* present = nullptr) {
  const bool has = request->hasParam(name, true);
  if (present) *present = has;
  return has ? request->getParam(name, true)->value() : String();
}

bool formFlag(AsyncWebServerRequest* request, const char* name, bool* present = nullptr) {
  // "false" і "0" - хиба, будь-що інше (зокрема "on" від чекбокса) - істина.
  const String value = formParam(request, name, present);
  return value != "false" && value != "0";
}

ProfileForm readProfileForm(AsyncWebServerRequest* request) {
  ProfileForm f;
  f.id = (uint16_t)strtoul(formParam(request, "id").c_str(), nullptr, 10);
  f.ssid = formParam(request, "ssid");
  f.password = formParam(request, "password", &f.hasPassword);
  f.priority = formParam(request, "priority", &f.hasPriority).toInt();
  f.enabled = formFlag(request, "enabled", &f.hasEnabled);
  f.maxRetries = formParam(request, "maxRetries", &f.hasMaxRetries).toInt();
  f.staticIp = formFlag(request, "staticIp", &f.hasStaticIp);
  f.ip = formParam(request, "ip");
  f.gateway = formParam(request, "gateway");
  f.subnet = formParam(request, "subnet");
  f.dns = formParam(request, "dns");
  f.connectNow = request->hasParam("connect", true) && formFlag(request, "connect");
  return f;
}

// Перевіряє адресу через IPAddress::fromString() - той самий розбір, яким її
// потім застосує NetworkSupervisor, тож "майже адреса" не проїде.
bool validIp(const String& value) {
  IPAddress parsed;
  return parsed.fromString(value);
}

// Порожній рядок - форма валідна; інакше текст помилки для 400.
String validateProfileForm(const ProfileForm& f) {
  if (f.ssid.length() == 0) return "Missing 'ssid' parameter";
  if (f.ssid.length() > 32) return "SSID is longer than 32 characters";
  // Порожній пароль дозволений і означає відкриту мережу; непорожній мусить
  // бути придатним для WPA, інакше профіль збережеться, а підключення
  // провалиться вже мовчки, під час підбору мережі.
  if (f.hasPassword && f.password.length() != 0 &&
      (f.password.length() < 8 || f.password.length() > 63)) {
    return "Password must be 8-63 characters";
  }
  if (f.hasPriority && (f.priority < -128 || f.priority > 127)) {
    return "Priority must be between -128 and 127";
  }
  if (f.hasMaxRetries && (f.maxRetries < -1 || f.maxRetries > 127)) {
    return "Retries must be between -1 and 127";
  }
  if (!f.hasStaticIp || !f.staticIp) return String();

  if (!validIp(f.ip)) return "Invalid IP address";
  if (!validIp(f.gateway)) return "Invalid gateway address";
  if (!validIp(f.subnet)) return "Invalid subnet mask";
  if (f.dns.length() != 0 && !validIp(f.dns)) return "Invalid DNS address";
  return String();
}

void applyProfileForm(WifiConnection& conn, const ProfileForm& f) {
  conn.ssid = f.ssid.c_str();
  if (f.hasPassword) conn.password = f.password.c_str();
  if (f.hasPriority) conn.priority = (int8_t)f.priority;
  if (f.hasEnabled) conn.isEnabled = f.enabled;
  if (f.hasMaxRetries) conn.maxRetries = (int8_t)f.maxRetries;
  if (!f.hasStaticIp) return;

  conn.staticIp = f.staticIp;
  // Адреси лишаємо в профілі й при поверненні на DHCP: вимкнути статику і
  // повернути її назад - звична пара дій, і вдруге вводити ті самі чотири
  // поля користувач не має.
  if (!f.staticIp) return;
  conn.ip = f.ip.c_str();
  conn.gateway = f.gateway.c_str();
  conn.subnet = f.subnet.c_str();
  conn.dns = f.dns.c_str();
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
    connections += ",\"maxRetries\":";
    connections += c.maxRetries;
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
  // створюється, якщо його немає, і оновлюється, якщо він уже є. Явний 'id'
  // потрібен лише редакторові - без нього перейменувати SSID не можна, бо
  // саме SSID і шукається.
  server.on("/api/wifi/connections", HTTP_POST, [this, &portal](AsyncWebServerRequest* request) {
    const ProfileForm form = readProfileForm(request);
    const String invalid = validateProfileForm(form);
    if (invalid.length() != 0) {
      request->send(400, "application/json", webjson::error(invalid.c_str()));
      return;
    }

    const uint32_t jobId = portal.jobs().submit([this, form]() -> String {
      WifiConnection* target =
          form.id != 0 ? _supervisor.getConnection(form.id) : _findBySsid(form.ssid);
      if (form.id != 0 && target == nullptr) return webjson::fail("No such profile");

      // Два профілі з однаковим SSID - тиха пастка: підбір мережі візьме
      // перший-ліпший, і правки в другому просто ніколи не спрацюють.
      WifiConnection* sameSsid = _findBySsid(form.ssid);
      if (sameSsid != nullptr && (target == nullptr || sameSsid->connectionId != target->connectionId)) {
        return webjson::fail("Another profile already uses this SSID");
      }

      uint16_t id;
      if (target != nullptr) {
        applyProfileForm(*target, form);
        id = target->connectionId;
      } else {
        WifiConnection conn;
        applyProfileForm(conn, form);
        id = _supervisor.addConnection(conn);
      }

      _supervisor.saveConfig();

      if (!form.connectNow) return webjson::ok("Profile saved");

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
