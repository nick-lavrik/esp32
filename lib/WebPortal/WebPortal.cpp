#include "WebPortal.hpp"

#include <ConfigStorage.hpp>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>

#include "WebJson.hpp"
#include "WebPortalAssets.hpp"

WebPortal::WebPortal(HttpServer& httpServer, ConfigStorage& storage)
    : _httpServer(httpServer),
      _storage(storage),
      _fsSource(LittleFS, WEB_PORTAL_FS_ROOT),
      _builtinSource(webassets::kAssets, webassets::kAssetCount) {}

void WebPortal::addModule(IWebModule* module) {
  if (module != nullptr) _modules.push_back(module);
}

void WebPortal::addStaticSource(IStaticSource* source, int priority) {
  _staticSources.addSource(source, priority);
}

void WebPortal::setCredentials(const String& user, const String& password) {
  _storage.setString(kCfgUser, user);
  _storage.setString(kCfgPassword, password);
}

void WebPortal::_registerCoreRoutes() {
  AsyncWebServer& server = _httpServer.server();

  server.on("/api/status", HTTP_GET, [this](AsyncWebServerRequest* request) {
    String json = "{\"uptimeMs\":";
    json += millis();
    json += ",\"freeHeap\":";
    json += (uint32_t)ESP.getFreeHeap();
    json += ",\"auth\":";
    json += webjson::boolean(_httpServer.hasAuth());
    json += ",\"pendingJobs\":";
    json += (uint32_t)_jobs.pending();
    json += ",\"modules\":[";
    for (size_t i = 0; i < _modules.size(); ++i) {
      if (i > 0) json += ',';
      json += webjson::quote(_modules[i]->name());
    }
    json += "]}";
    request->send(200, "application/json", json);
  });

  // Єдина точка отримання результату будь-якої задачі: і команди консолі, і
  // мережевої операції. Клієнт опитує її, поки status != "done".
  server.on("/api/job", HTTP_GET, [this](AsyncWebServerRequest* request) {
    if (!request->hasParam("id")) {
      request->send(400, "application/json", webjson::error("Missing 'id' parameter"));
      return;
    }

    const uint32_t id = strtoul(request->getParam("id")->value().c_str(), nullptr, 10);
    String result;
    const WebJobQueue::Status status = _jobs.status(id, result);

    String json = "{\"id\":";
    json += id;
    json += ",\"status\":";
    switch (status) {
      case WebJobQueue::Status::QUEUED: json += "\"queued\""; break;
      case WebJobQueue::Status::RUNNING: json += "\"running\""; break;
      case WebJobQueue::Status::DONE:
        json += "\"done\",\"result\":";
        // Модулі кладуть у результат готовий JSON, тому вставляємо як є.
        json += result.length() ? result : String("null");
        break;
      // Задача або не існувала, або її результат уже витіснений новішими.
      // Для клієнта це одне й те саме: чекати більше нема чого.
      case WebJobQueue::Status::UNKNOWN: json += "\"expired\""; break;
    }
    json += "}";

    request->send(200, "application/json", json);
  });

  // Зміна пароля порталу. Мутація NVS - але коротка (два ключі), тому
  // без черги: flash-запис на пару десятків байтів не зупиняє сервер так,
  // як скан ефіру.
  server.on("/api/auth", HTTP_POST, [this](AsyncWebServerRequest* request) {
    const String user = request->hasParam("user", true) ? request->getParam("user", true)->value()
                                                        : String();
    const String password =
        request->hasParam("password", true) ? request->getParam("password", true)->value() : String();

    if (user.length() > 0 && password.length() == 0) {
      request->send(400, "application/json", webjson::error("Password must not be empty"));
      return;
    }

    setCredentials(user, password);
    _logger.warn("portal credentials changed (auth %s), restart required",
                 password.length() ? "enabled" : "disabled");

    request->send(200, "application/json",
                  String("{\"ok\":true,\"authEnabled\":") + webjson::boolean(password.length() > 0) +
                      ",\"note\":\"Restart the device to apply\"}");
  });
}

bool WebPortal::begin() {
  // LittleFS - основне джерело, вшита сторінка - останній рубіж.
  _staticSources.addSource(&_fsSource, 100);
  _staticSources.addSource(&_builtinSource, -100);
  _httpServer.setStaticSource(&_staticSources);

  const String user = _storage.getString(kCfgUser, "");
  const String password = _storage.getString(kCfgPassword, "");
  _httpServer.setAuth(user, password);

  _registerCoreRoutes();

  AsyncWebServer& server = _httpServer.server();
  for (IWebModule* module : _modules) {
    module->registerRoutes(server, *this);
  }

  if (!_httpServer.begin()) {
    _logger.error("HTTP server failed to start");
    return false;
  }

  _logger.info("portal started on port 80, auth %s, %u module(s)",
               _httpServer.hasAuth() ? "on" : "off", (unsigned)_modules.size());
  if (!LittleFS.exists(WEB_PORTAL_FS_ROOT "/index.html")) {
    _logger.warn("no " WEB_PORTAL_FS_ROOT "/index.html in LittleFS, serving built-in page");
  }
  return true;
}

void WebPortal::loop() {
  _jobs.loop();
  for (IWebModule* module : _modules) {
    module->loop();
  }
}
