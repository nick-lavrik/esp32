#include "NetworkSetup.hpp"

#if defined(BOARD_ESP8266)
#include <ESP8266WiFi.h>
#else
#include <WiFi.h>
#endif
#include <time.h>

#include <Logger.hpp>
#include <NetworkSupervisor.hpp>
#include <TLogger.hpp>
#include <string>

#include "App/AppGlobals.hpp"
#include "NetCli.hpp"
#include "WifiNetworks.hpp"

// Єдиний listener у прошивці: перекладає події FSM у лог. Усе інше в коді
// питає стан у глобального WiFi (WiFi.isConnected() тощо) - воно працює
// однаково, хто б не викликав begin().
namespace {

struct NetworkEventLogger : public INetworkSupervisorListener {
  const TLogger logger{"net"};

  void onConnecting(const WifiConnection& conn) override { logger.info("connecting to '%s'...", conn.ssid.c_str()); }
  void onConnected(const WifiConnection& conn, const std::string& ip) override {
    logger.info("connected to '%s', IP %s (%d dBm)", conn.ssid.c_str(), ip.c_str(), WiFi.RSSI());
  }
  void onDisconnected(const std::string& ssid) override { logger.warn("disconnected from '%s'", ssid.c_str()); }
  void onConnectionFailed(const WifiConnection& conn) override {
    logger.warn("failed to connect to '%s'", conn.ssid.c_str());
  }
  void onApStarted(const std::string& apSsid, const std::string& ip) override {
    logger.info("hotspot '%s' up at %s, still scanning for known networks", apSsid.c_str(), ip.c_str());
  }
  void onApStopped() override { logger.info("hotspot down"); }
};

NetworkEventLogger networkEventLogger;

}  // namespace

// Замість колишнього setupWiFi(): підняти NetworkSupervisor і віддати йому
// радіо. Виклик неблокуючий, як і раніше - FSM крутиться у власному
// FreeRTOS-таску, а все, що нижче в setup(), і так стоїть під
// WiFi.isConnected()-гардами.
void setupNetworkSupervisor() {
  NetworkSupervisorConfig cfg;
  // Ім'я env у SSID точки доступу: у мережі часто крутиться кілька плат.
  cfg.apSsid = std::string("ESP-") + PIO_PIOENV;
  cfg.apFallbackEnabled = true;
  // DHCP hostname = ім'я env за замовчуванням (WIFI_HOSTNAME, секрети),
  // щоб "ping esp32-c6-lcd096" резолвився без окремої настройки. Runtime-
  // перевизначення - 'net general hostname <name>', loadConfig() нижче
  // підхопить його з NVS і перекриє цей дефолт.
  cfg.hostname = WIFI_HOSTNAME;
  netSupervisor.setConfig(cfg);

  // Список мереж живе в NVS; loadConfig() перекриє щойно виставлений cfg
  // збереженим, якщо він там є.
  netSupervisor.loadConfig();

  // Порожній список - пробуємо файли з LittleFS (/network/*.nmconnection,
  // формат keyfile NetworkManager). Їх кладуть з компа через
  // 'pio run -t uploadfs', тобто плату можна спорядити мережами не
  // перекомпільовуючи прошивку й не набираючи нічого в консолі.
  //
  // Саме ЗАСІВ, а не постійне джерело: далі список живе в NVS, бо uploadfs
  // перезаписує розділ цілком і поховав би все додане командою 'net'.
  // Перечитати файли будь-коли - 'net connection reload'.
  if (netSupervisor.connections().empty()) {
    const size_t imported = importNetProfilesFromFs(netSupervisor);
    if (imported > 0) {
      Logger::info("NetworkSupervisor imported %u profile(s) from LittleFS", (unsigned)imported);
    }
  }

  // Остання лінія оборони: прошитий перелік (src/WifiNetworks.hpp), щоб пристрій
  // не лишився без зв'язку після чистої прошивки з порожнім LittleFS.
  //
  // Свідомо на КОЖНОМУ старті, а не лише на порожній список: інакше додана в
  // таблицю мережа доїжджала б на плату тільки через erase-flash, а зміна
  // пароля в secrets.ini мовчки не мала б жодного ефекту. seedConnections()
  // додає лише відсутні SSID і не чіпає вже збережені - тому 'net connection
  // modify' не відкочується. Виняток один: видалену командою 'net connection
  // delete' прошиту мережу наступний ребут поверне; щоб вимкнути її назовсім -
  // 'net connection modify <id> connection.autoconnect no'.
  if (netSupervisor.seedConnections(kWifiNetworks) > 0) netSupervisor.saveConfig();

  // lastConnected інакше рахувався б від millis() і обнулявся на кожному
  // ребуті - тоді збережений порядок "останній вдалий першим" після рестарту
  // ставав би випадковим. ntp ще не стартував, але лямбда ліниво питає час
  // у момент підключення.
  netSupervisor.setClock([]() -> uint32_t { return ntp.isSynced() ? static_cast<uint32_t>(time(nullptr)) : 0u; });

  netSupervisor.addListener(&networkEventLogger);
  netSupervisor.begin();
  Logger::info("NetworkSupervisor started with %u profile(s)", (unsigned)netSupervisor.connections().size());
}
