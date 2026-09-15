#include "WebCommandsModule.hpp"

#include <ConfigStorage.hpp>
#include <ESPAsyncWebServer.h>

#include "WebJson.hpp"
#include "WebPortal.hpp"

namespace {

// Роздільник полів у NVS-елементі. Табуляція не трапляється ні в назві
// shortcut'а (її вводять у текстове поле), ні в рядку команди - SerialCommander
// ріже аргументи по пробілах.
constexpr char kFieldSeparator = '\t';

}  // namespace

WebCommandsModule::WebCommandsModule(SerialCommander& commander, Submit submit,
                                     ConfigStorage& storage)
    : _commander(commander), _submit(std::move(submit)), _storage(storage) {
#if defined(ESP32)
  _mutex = xSemaphoreCreateMutex();
#endif
}

WebCommandsModule::~WebCommandsModule() {
#if defined(ESP32)
  if (_mutex) vSemaphoreDelete(_mutex);
#endif
}

void WebCommandsModule::_loadShortcuts() {
  std::vector<String> raw;
  _storage.getStringArray(kCfgShortcuts, raw);

  _shortcuts.clear();
  for (const String& item : raw) {
    const int sep = item.indexOf(kFieldSeparator);
    // Елемент без роздільника - слід чужого запису або зіпсованого блоба.
    // Пропускаємо мовчки: відсутній shortcut помітно в UI, а зайвий warn на
    // кожному старті - ні.
    if (sep <= 0) continue;
    if (_shortcuts.size() >= kMaxShortcuts) break;
    _shortcuts.push_back({item.substring(0, sep), item.substring(sep + 1)});
  }

  _rebuildJson();
}

void WebCommandsModule::_rebuildJson() {
  String json = "[";
  for (size_t i = 0; i < _shortcuts.size(); ++i) {
    if (i > 0) json += ',';
    json += "{\"index\":";
    json += (uint32_t)i;
    json += ",\"name\":";
    json += webjson::quote(_shortcuts[i].name);
    json += ",\"command\":";
    json += webjson::quote(_shortcuts[i].command);
    json += "}";
  }
  json += "]";

  Lock lock(_mutex);
  _shortcutsJson = std::move(json);
}

String WebCommandsModule::_saveJob(const String& name, const String& command, int index) {
  if (index < 0 && _shortcuts.size() >= kMaxShortcuts) {
    return webjson::fail("Shortcut list is full");
  }
  if (index >= 0 && (size_t)index >= _shortcuts.size()) {
    return webjson::fail("No such shortcut");
  }

  if (index >= 0) {
    _shortcuts[index] = {name, command};
  } else {
    _shortcuts.push_back({name, command});
  }

  std::vector<String> raw;
  raw.reserve(_shortcuts.size());
  for (const Shortcut& s : _shortcuts) raw.push_back(s.name + kFieldSeparator + s.command);
  _storage.setStringArray(kCfgShortcuts, raw);

  _rebuildJson();
  return webjson::ok(index >= 0 ? "Shortcut updated" : "Shortcut saved");
}

String WebCommandsModule::_deleteJob(size_t index) {
  if (index >= _shortcuts.size()) return webjson::fail("No such shortcut");

  _shortcuts.erase(_shortcuts.begin() + index);

  std::vector<String> raw;
  raw.reserve(_shortcuts.size());
  for (const Shortcut& s : _shortcuts) raw.push_back(s.name + kFieldSeparator + s.command);
  _storage.setStringArray(kCfgShortcuts, raw);

  _rebuildJson();
  return webjson::ok("Shortcut removed");
}

void WebCommandsModule::registerRoutes(AsyncWebServer& server, WebPortal& portal) {
  // registerRoutes() кличеться з setup(), тобто з того самого контексту, що й
  // loop() - читати NVS тут можна.
  _loadShortcuts();

  // ---- перелік зареєстрованих команд ----
  //
  // Шлях '/api/commands/list', а НЕ '/api/commands', і це не косметика.
  // AsyncCallbackWebHandler::canHandle() вважає свій uri префіксом:
  //   _uri != request->url() && !request->url().startsWith(_uri + "/")
  // (WebHandlers.cpp:294). Тобто маршрут '/api/commands' перехоплює й
  // '/api/commands/shortcuts' - перевірено на залізі: GET shortcuts віддавав
  // список команд. Мовчки, бо обидва - валідний JSON.
  //
  // Реєстр SerialCommander наповнюється в setup() і далі не міняється, тому
  // читати його з таска сервера безпечно - на відміну від списку зʼєднань
  // NetworkSupervisor, знімок тут не потрібен.
  server.on("/api/commands/list", HTTP_GET, [this](AsyncWebServerRequest* request) {
    String json = "[";
    for (size_t i = 0; i < _commander.commandCount(); ++i) {
      if (i > 0) json += ',';
      json += "{\"name\":";
      json += webjson::quote(_commander.commandName(i));
      json += ",\"description\":";
      json += webjson::quote(_commander.commandDescription(i));
      json += "}";
    }
    json += "]";
    request->send(200, "application/json", json);
  });

  // ---- запуск команди ----
  //
  // Єдиний веб-вхід для "виконати рядок": сюди шле і ця вкладка, і поле вводу
  // консолі.
  server.on("/api/commands/exec", HTTP_POST, [this](AsyncWebServerRequest* request) {
    if (!request->hasParam("cmd", true)) {
      request->send(400, "application/json", webjson::error("Missing 'cmd' parameter"));
      return;
    }

    String cmd = request->getParam("cmd", true)->value();
    cmd.trim();
    if (cmd.length() == 0) {
      request->send(400, "application/json", webjson::error("Empty command"));
      return;
    }
    // Мовчки обрізати рядок не можна: виконалась би не та команда, яку ввели.
    if (cmd.length() >= kMaxLine) {
      request->send(400, "application/json", webjson::error("Command line is too long"));
      return;
    }

    // Відлуння введеного рядка - щоб у консолі було видно, ЩО саме виконали,
    // як у терміналі. Іде звичайним логом, тож рядок бачать усі приймачі, і
    // serial теж: у моніторі видно, що команду запустили з браузера.
    //
    // Логуємо ДО submit(): при повній черзі в стрічці має лишитись слід, що
    // команду вводили, а не тиша.
    _logger.info("> %s", cmd.c_str());

    if (!_submit || !_submit(cmd.c_str())) {
      request->send(503, "application/json", webjson::error("Command queue is full, try again"));
      return;
    }

    request->send(202, "application/json", String("{\"queued\":") + webjson::quote(cmd) + "}");
  });

  // ---- shortcuts ----
  server.on("/api/commands/shortcuts", HTTP_GET, [this](AsyncWebServerRequest* request) {
    Lock lock(_mutex);
    request->send(200, "application/json", _shortcutsJson);
  });

  server.on("/api/commands/shortcuts", HTTP_POST, [this, &portal](AsyncWebServerRequest* request) {
    if (!request->hasParam("name", true) || !request->hasParam("command", true)) {
      request->send(400, "application/json",
                    webjson::error("Missing 'name' or 'command' parameter"));
      return;
    }

    String name = request->getParam("name", true)->value();
    String command = request->getParam("command", true)->value();
    name.trim();
    command.trim();

    if (name.length() == 0 || command.length() == 0) {
      request->send(400, "application/json", webjson::error("Name and command must not be empty"));
      return;
    }
    if (name.length() > kMaxShortcutName || command.length() >= kMaxLine) {
      request->send(400, "application/json", webjson::error("Name or command is too long"));
      return;
    }
    // Табуляція розділяє поля в NVS-елементі: пропустити її означало б
    // розʼїхатись при наступному читанні.
    if (name.indexOf(kFieldSeparator) >= 0 || command.indexOf(kFieldSeparator) >= 0) {
      request->send(400, "application/json", webjson::error("Tab character is not allowed"));
      return;
    }

    // Без 'index' - новий shortcut, з 'index' - перезапис наявного.
    const int index =
        request->hasParam("index", true) ? request->getParam("index", true)->value().toInt() : -1;

    const uint32_t jobId = portal.jobs().submit(
        [this, name, command, index]() { return _saveJob(name, command, index); });
    if (jobId == 0) {
      request->send(503, "application/json", webjson::error("Job queue is full, try again"));
      return;
    }
    request->send(202, "application/json", String("{\"jobId\":") + jobId + "}");
  });

  server.on("/api/commands/shortcuts", HTTP_DELETE, [this, &portal](AsyncWebServerRequest* request) {
    if (!request->hasParam("index")) {
      request->send(400, "application/json", webjson::error("Missing 'index' parameter"));
      return;
    }

    const size_t index = strtoul(request->getParam("index")->value().c_str(), nullptr, 10);
    const uint32_t jobId = portal.jobs().submit([this, index]() { return _deleteJob(index); });
    if (jobId == 0) {
      request->send(503, "application/json", webjson::error("Job queue is full, try again"));
      return;
    }
    request->send(202, "application/json", String("{\"jobId\":") + jobId + "}");
  });
}
