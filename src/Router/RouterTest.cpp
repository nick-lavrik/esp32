#include "RouterTest.hpp"

#include <LittleFS.h>

#include <Logger.hpp>
#include <RouterApiClient.hpp>
#include <RouterClientListIterator.hpp>
#include <RouterClientListParser.hpp>
#include <vector>

namespace {
// Хост і base64(login:password) приходять із secrets.ini через build_flags
// (ROUTER_HOST / ROUTER_LOGIN_AUTHORIZATION) - раніше вони були захардкожені
// тут, у файлі під git, попри те що механізм для секретів уже існував.
RouterApiClient routerApi(ROUTER_HOST, ROUTER_LOGIN_AUTHORIZATION);

void dumpAsusClientList(String& json) {
  std::vector<RouterClientInfo> clients;
  if (!RouterClientListParser::parse(json, clients)) {
    Logger::error("can't parse client list json. [%d]", clients.capacity());
  }

  RouterClientListIterator it(std::move(clients));
  while (it.hasNext()) {
    const RouterClientInfo& c = it.next();
    Logger::info("client=%-30s timer=%9s", c.name.c_str(), c.timer.c_str());
  }
}

void testAsusWRT() {
  Logger::info("====== AsusWRT test script =======");
  Logger::info("free heap: %u", ESP.getFreeHeap());
  if (!routerApi.login()) {
    Logger::error("AsusWRT login fail");
    return;
  }
  String json;
  if (!routerApi.fetchClientListJson(json)) {
    Logger::error("AsusWRT fetch client fail");
  }
  dumpAsusClientList(json);
  Logger::info("------ AsusWRT test script -------");
  Logger::info("");
}

// Офлайн-варіант testAsusWRT(): той самий розбір, але JSON береться з
// LittleFS, а не з роутера. Потрібен, коли роутер недоступний - перевірити,
// що парсер ще розуміє формат get_clientlist (зразок кладеться uploadfs).
void testAsusWRT2() {
  Logger::info("====== AsusWRT test script =======");
  Logger::info("free heap: %u", ESP.getFreeHeap());

  const char* path = "/asus-get_clientlist.json";
  File file = LittleFS.open(path, "r");
  if (!file || file.isDirectory()) {
    Logger::error("Can't open file (%s)", path);
    return;
  }

  String json = file.readString();
  file.close();
  dumpAsusClientList(json);
  Logger::info("------ AsusWRT test script -------");
  Logger::info("");
}
}  // namespace

void registerRouterCommands(SerialCommander& commander) {
  commander.registerCommand("dump-asuswrt", "test AsusWRT", [](const String& args) { testAsusWRT(); });
  commander.registerCommand("dump-asuswrt2", "test AsusWRT parser on /asus-get_clientlist.json (LittleFS)",
                            [](const String& args) { testAsusWRT2(); });
}
