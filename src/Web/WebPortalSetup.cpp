#include "WebPortalSetup.hpp"

#if HAS_WEB_PORTAL

#include <WiFi.h>

#include <Logger.hpp>
#include <TLogger.hpp>

#include "App/AppGlobals.hpp"

// Піднімає портал ОДИН раз і назавжди. Свідомо без жодної перевірки
// WiFi.isConnected(): AsyncWebServer слухає на всіх інтерфейсах lwIP, тож той
// самий сервер обслуговує і домашню мережу, і AP-fallback. Прив'язка до
// стану мережі лише створила б вікно, коли пристрій уже підняв точку доступу,
// а портал на ній ще не відповідає.
void setupWebPortal() {
  webPortal.addModule(&webWifiModule);
  webPortal.addModule(&webConsoleModule);
  webPortal.addModule(&webCommandsModule);
  webPortal.addModule(&webNvsModule);
  webPortal.addModule(&webFilesModule);
  webPortal.addModule(&webSystemModule);
#if HAS_SCREEN_MIRROR
  webPortal.addModule(&webScreenModule);
#endif
#if HAS_MQTT_CLIENT
  webPortal.addModule(&webMqttModule);
#endif
#if HAS_ECOFLOW_CLIENT
  webPortal.addModule(&webEcoflowModule);
#endif

  if (!webPortal.begin()) {
    Logger::error("WebPortal setup failed");
    return;
  }
  Logger::info("WebPortal setup done");
}

void registerWebCommands(SerialCommander& commander) {
  // Пароль порталу інакше можна було б задати лише з самого порталу - тобто
  // з відкритої сторінки, яку до першого пароля бачить уся мережа. Тому
  // креденшели ставляться з консолі.
  commander.registerCommand("web", "web portal: status | auth <user> <pass> | auth off", [](const String args) {
    static TLogger _log{"web"};
    String rest = args;
    rest.trim();

    if (rest.length() == 0 || rest.startsWith("status")) {
      _log.info("server   : %s", webPortal.isRunning() ? "running" : "stopped");
      _log.info("auth     : %s", httpServer.hasAuth() ? "on (HTTP Basic)" : "off - open to everyone");
      _log.info("jobs     : %u pending", (unsigned)webPortal.jobs().pending());
      if (WiFi.isConnected()) _log.info("url      : http://%s/", WiFi.localIP().toString().c_str());
      if (netSupervisor.state() == NetworkSupervisorState::AP_MODE) {
        _log.info("hotspot  : http://%s/", WiFi.softAPIP().toString().c_str());
      }
      return;
    }

    if (!rest.startsWith("auth")) {
      _log.warn("use: web status | web auth <user> <pass> | web auth off");
      return;
    }

    rest = rest.substring(4);
    rest.trim();

    if (rest == "off") {
      webPortal.setCredentials("", "");
      _log.warn("auth disabled, restart to apply");
      return;
    }

    const int space = rest.indexOf(' ');
    if (space <= 0) {
      _log.warn("use: web auth <user> <pass> | web auth off");
      return;
    }

    String user = rest.substring(0, space);
    String pass = rest.substring(space + 1);
    user.trim();
    pass.trim();
    if (user.length() == 0 || pass.length() == 0) {
      _log.warn("both user and password are required");
      return;
    }

    webPortal.setCredentials(user, pass);
    _log.warn("credentials saved for '%s', restart to apply", user.c_str());
  });
}

#endif  // HAS_WEB_PORTAL
