#pragma once

// Команда 'net' - керування NetworkSupervisor із serial/MQTT-консолі.
//
// Свідомо дзеркалить nmcli: та сама структура "ОБ'ЄКТ ДІЯ [аргументи]", ті самі
// імена налаштувань (connection.autoconnect-priority, ipv4.method, ...) і те
// саме скорочення слів до унікального префікса ('net c s' == 'net connection
// show'). Довідник по самому nmcli лежить у docs/network_manager_guide.md.
//
// Відмінності від nmcli, які варто памʼятати:
//   - профіль адресується числовим id або SSID (UUID/con-name тут немає);
//   - 'connection add' одразу пише в NVS, окремого 'save' немає;
//   - 'radio wifi off' зупиняє FSM цілком (NetworkSupervisor::end()).
//
// Де живе конфігурація (важливо, бо сховищ два):
//   NVS     - робоче. Його читає FSM, у нього пишуть команди. Переживає і
//             'pio run -t upload', і 'pio run -t uploadfs'.
//   LittleFS- /network/*.nmconnection, формат keyfile NetworkManager.
//             Це джерело ПОСТАЧАННЯ: файли кладуться з компа через
//             'pio run -t uploadfs'. Але uploadfs перезаписує розділ цілком,
//             тому все, що додали на пристрої командою, там би загинуло -
//             саме тому робочим сховищем лишається NVS, а файли лише
//             імпортуються: автоматично на порожній список і будь-коли
//             вручну через 'net connection reload' / 'load'.

#include <Arduino.h>
#include <LittleFS.h>

#include <NetworkSupervisor.hpp>
#include <NmConnectionIni.hpp>
#include <SerialCommander.hpp>
#include <TLogger.hpp>
#include <string>
#include <vector>

#include "wifi.h"

namespace netcli {

static const TLogger logger{"net"};

// ---------------------------------------------------------------------------
// Розбір аргументів
// ---------------------------------------------------------------------------

// SerialCommander віддає все після імені команди одним рядком, а токенайзера в
// проєкті немає. Лапки тут не примха: у secrets.ini живий SSID - "Asus " (з
// пробілом на кінці), і без них його не ввести.
static void splitArgs(const String& line, std::vector<String>& out) {
  out.clear();
  String current;
  bool inQuotes = false;
  bool started = false;  // щоб "" давало порожній токен, а не зникало

  for (size_t i = 0; i < line.length(); ++i) {
    const char c = line[i];
    if (c == '"') {
      inQuotes = !inQuotes;
      started = true;
    } else if (!inQuotes && isspace(static_cast<unsigned char>(c))) {
      if (started) out.push_back(current);
      current = "";
      started = false;
    } else {
      current += c;
      started = true;
    }
  }
  if (started) out.push_back(current);
}

// Скорочення до унікального префікса, як у nmcli.
// Повертає індекс збігу, -1 якщо не знайдено, -2 якщо неоднозначно.
static int match(const String& token, const char* const* candidates, size_t count) {
  if (token.length() == 0) return -1;

  int found = -1;
  for (size_t i = 0; i < count; ++i) {
    const String candidate(candidates[i]);
    if (candidate.equalsIgnoreCase(token)) return static_cast<int>(i);  // точний збіг виграє
    if (candidate.length() < token.length()) continue;
    if (candidate.substring(0, token.length()).equalsIgnoreCase(token)) {
      if (found >= 0) return -2;
      found = static_cast<int>(i);
    }
  }
  return found;
}

static void reportMatch(int result, const String& token, const char* const* candidates,
                        size_t count) {
  if (result == -2) {
    String variants;
    for (size_t i = 0; i < count; ++i) {
      const String candidate(candidates[i]);
      if (candidate.length() < token.length()) continue;
      if (!candidate.substring(0, token.length()).equalsIgnoreCase(token)) continue;
      if (variants.length()) variants += ", ";
      variants += candidates[i];
    }
    if (variants.length() == 0) {
      // Збіг знайшов matchSetting() посекційно - простим префіксом варіанти
      // не відновити, тож показуємо весь перелік.
      logger.warn("ambiguous '%s', pick one of:", token.c_str());
      for (size_t i = 0; i < count; ++i) logger.warn("  %s", candidates[i]);
      return;
    }
    logger.warn("ambiguous '%s': %s", token.c_str(), variants.c_str());
    return;
  }
  String variants;
  for (size_t i = 0; i < count; ++i) {
    if (variants.length()) variants += "|";
    variants += candidates[i];
  }
  if (variants.length() <= 90) {
    logger.warn("unknown '%s', expected: %s", token.c_str(), variants.c_str());
    return;
  }
  // Довгий перелік (як у 'modify') в один рядок логу не влазить - його
  // обрізало б рівно на найцікавішому місці.
  logger.warn("unknown '%s', expected one of:", token.c_str());
  for (size_t i = 0; i < count; ++i) logger.warn("  %s", candidates[i]);
}

// Матчер для nmcli-івських імен налаштувань "секція.властивість".
// На відміну від match(), скорочувати можна КОЖНУ частину окремо:
// 'wifi-sec.psk' і 'con.autoconnect-priority' - валідні, хоча префіксом
// повного імені жодне з них не є. Секцію можна опустити зовсім: 'psk'.
static int matchSetting(const String& token, const char* const* candidates, size_t count) {
  if (token.length() == 0) return -1;

  for (size_t i = 0; i < count; ++i) {
    if (token.equalsIgnoreCase(candidates[i])) return static_cast<int>(i);
  }

  const int dot = token.indexOf('.');
  const String sect = dot < 0 ? String("") : token.substring(0, dot);
  const String prop = dot < 0 ? token : token.substring(dot + 1);

  // Два проходи: спершу шукаємо точний збіг властивості, і лише якщо його
  // немає - збіг за префіксом. Інакше 'autoconnect' було б неоднозначним
  // через autoconnect-priority/-retries.
  for (int exactPass = 1; exactPass >= 0; --exactPass) {
    int found = -1;
    for (size_t i = 0; i < count; ++i) {
      const String full(candidates[i]);
      const int cdot = full.indexOf('.');
      const String csect = cdot < 0 ? String("") : full.substring(0, cdot);
      const String cprop = cdot < 0 ? full : full.substring(cdot + 1);

      if (sect.length() > 0) {
        if (csect.length() < sect.length()) continue;
        if (!csect.substring(0, sect.length()).equalsIgnoreCase(sect)) continue;
      }
      if (exactPass) {
        if (!cprop.equalsIgnoreCase(prop)) continue;
      } else {
        if (cprop.length() < prop.length()) continue;
        if (!cprop.substring(0, prop.length()).equalsIgnoreCase(prop)) continue;
      }

      if (found >= 0) return -2;
      found = static_cast<int>(i);
    }
    if (found >= 0) return found;
  }
  return -1;
}

// Іменований аргумент у стилі nmcli: "... password secret priority 5".
// Повертає порожній рядок, якщо ключа немає.
static String namedArg(const std::vector<String>& args, size_t from, const char* key,
                       bool* present = nullptr) {
  if (present) *present = false;
  for (size_t i = from; i + 1 < args.size(); ++i) {
    if (args[i].equalsIgnoreCase(key)) {
      if (present) *present = true;
      return args[i + 1];
    }
  }
  return String();
}

static bool parseBool(const String& v, bool& out) {
  if (v.equalsIgnoreCase("yes") || v.equalsIgnoreCase("on") || v.equalsIgnoreCase("true") ||
      v == "1") {
    out = true;
    return true;
  }
  if (v.equalsIgnoreCase("no") || v.equalsIgnoreCase("off") || v.equalsIgnoreCase("false") ||
      v == "0") {
    out = false;
    return true;
  }
  return false;
}

// ---------------------------------------------------------------------------
// Форматування
// ---------------------------------------------------------------------------

static const char* stateName(NetworkSupervisorState s) {
  switch (s) {
    case NetworkSupervisorState::IDLE:         return "unmanaged";
    case NetworkSupervisorState::SCANNING:     return "scanning";
    case NetworkSupervisorState::CONNECTING:   return "connecting";
    case NetworkSupervisorState::CONNECTED:    return "connected";
    case NetworkSupervisorState::RECONNECTING: return "reconnecting";
    case NetworkSupervisorState::WPS_WAITING:  return "wps-waiting";
    case NetworkSupervisorState::STARTING_AP:  return "starting-ap";
    case NetworkSupervisorState::AP_MODE:      return "hotspot";
  }
  return "unknown";
}

// Профіль за id (число) або SSID - як 'nmcli con up <name|uuid>'.
static WifiConnection* find(NetworkSupervisor& ns, const String& key) {
  bool numeric = key.length() > 0;
  for (size_t i = 0; i < key.length(); ++i) {
    if (!isdigit(static_cast<unsigned char>(key[i]))) {
      numeric = false;
      break;
    }
  }
  if (numeric) {
    WifiConnection* byId = ns.getConnection(static_cast<uint16_t>(key.toInt()));
    if (byId) return byId;
  }
  for (const auto& c : ns.connections()) {
    if (key == c.ssid.c_str()) {
      return ns.getConnection(c.connectionId);
    }
  }
  return nullptr;
}

static std::vector<std::string> knownSsids(NetworkSupervisor& ns) {
  std::vector<std::string> out;
  for (const auto& c : ns.connections()) out.push_back(c.ssid);
  return out;
}

// ---------------------------------------------------------------------------
// Профілі у LittleFS (.nmconnection)
// ---------------------------------------------------------------------------

// Каталог з профілями. Короткий свідомо: на ESP8266 LittleFS обмежує довжину
// компонента шляху, а до імені ще додається ".nmconnection" (13 символів).
static const char kProfileDir[] = "/network";

static bool isProfileFile(const String& name) { return name.endsWith(".nmconnection"); }

// Імпорт одного файлу. Мержимо за SSID: наявний профіль оновлюється на місці
// (id і lastConnected зберігаються), новий - додається.
// Повертає false, якщо файл не читається або це не Wi-Fi профіль.
static bool importFile(NetworkSupervisor& ns, const String& path, bool* created = nullptr) {
  File f = LittleFS.open(path, "r");
  if (!f || f.isDirectory()) {
    logger.warn("cannot read %s", path.c_str());
    if (f) f.close();
    return false;
  }
  std::string text;
  text.reserve(f.size());
  while (f.available()) text += static_cast<char>(f.read());
  f.close();

  WifiConnection parsed;
  std::string error;
  if (!NmConnectionIni::parse(text, parsed, &error)) {
    logger.warn("%s: %s", path.c_str(), error.c_str());
    return false;
  }

  for (const auto& c : ns.connections()) {
    if (c.ssid == parsed.ssid) {
      WifiConnection* existing = ns.getConnection(c.connectionId);
      if (!existing) break;
      const uint16_t keepId = existing->connectionId;
      const uint32_t keepLast = existing->lastConnected;
      *existing = parsed;
      existing->connectionId = keepId;
      existing->lastConnected = keepLast;  // історія підключень - не з файлу
      if (created) *created = false;
      return true;
    }
  }

  ns.addConnection(parsed);
  if (created) *created = true;
  return true;
}

// Імпорт усього каталогу. Повертає кількість успішно прочитаних профілів.
static size_t importDir(NetworkSupervisor& ns, size_t* added = nullptr) {
  size_t ok = 0, newOnes = 0;

  File dir = LittleFS.open(kProfileDir, "r");  // ESP8266 вимагає режим явно
  if (!dir || !dir.isDirectory()) {
    if (dir) dir.close();
    if (added) *added = 0;
    return 0;
  }

  for (File entry = dir.openNextFile(); entry; entry = dir.openNextFile()) {
    const String name = String(entry.name());
    const bool isDir = entry.isDirectory();
    entry.close();
    if (isDir || !isProfileFile(name)) continue;

    // openNextFile() віддає то повний шлях, то саме ім'я - залежно від
    // платформи й версії ядра. Нормалізуємо.
    const String path = name.startsWith("/") ? name : String(kProfileDir) + "/" + name;

    bool created = false;
    if (importFile(ns, path, &created)) {
      ++ok;
      if (created) ++newOnes;
    }
  }
  dir.close();

  if (added) *added = newOnes;
  return ok;
}

// Запис профілю у файл. Повертає шлях або порожній рядок при помилці.
static String exportOne(const WifiConnection& conn) {
  if (!LittleFS.exists(kProfileDir)) LittleFS.mkdir(kProfileDir);

  const String path = String(kProfileDir) + "/" + NmConnectionIni::fileNameFor(conn).c_str();
  File f = LittleFS.open(path, "w");
  if (!f) {
    logger.warn("cannot write %s", path.c_str());
    return String();
  }
  const std::string text = NmConnectionIni::serialize(conn);
  const size_t written = f.print(text.c_str());
  f.close();

  if (written != text.size()) {
    logger.warn("%s: short write (%u of %u B)", path.c_str(), (unsigned)written,
                (unsigned)text.size());
    return String();
  }
  return path;
}

// ---------------------------------------------------------------------------
// Довідка
// ---------------------------------------------------------------------------

static void printHelp() {
  logger.info("Usage: net OBJECT { COMMAND | help } [ARGUMENTS...]");
  logger.info("  OBJECT and COMMAND may be abbreviated: 'net c s' == 'net connection show'");
  logger.info("");
  logger.info("net general status                    overall network state");
  logger.info("net radio wifi [on|off]               enable/disable the supervisor");
  logger.info("net device status                     interface state");
  logger.info("net device wifi list                  scan the air");
  logger.info("net device wifi connect <ssid> [password <p>]");
  logger.info("net device wifi hotspot [ssid <s>] [password <p>]");
  logger.info("net device disconnect                 drop the current connection");
  logger.info("net connection show [<id|ssid>]       saved profiles / one profile");
  logger.info("net connection add ssid <s> [password <p>] [priority <n>]");
  logger.info("net connection modify <id|ssid> <setting> <value>");
  logger.info("net connection delete <id|ssid>");
  logger.info("net connection up <id|ssid>           connect to this profile");
  logger.info("net connection down                   disconnect");
  logger.info("net connection reload                 re-read /network/*.nmconnection");
  logger.info("net connection load <file>            import one .nmconnection file");
  logger.info("net connection export [<id|ssid>]     write profiles out as files");
  logger.info("");
  logger.info("settings for 'modify': wifi.ssid, wifi-security.psk, connection.autoconnect,");
  logger.info("  connection.autoconnect-priority, connection.autoconnect-retries,");
  logger.info("  ipv4.method (auto|manual), ipv4.addresses (CIDR), ipv4.gateway, ipv4.dns");
}

// ---------------------------------------------------------------------------
// general
// ---------------------------------------------------------------------------

static void generalStatus(NetworkSupervisor& ns) {
  const bool up = ns.isConnected();
  logger.info("STATE       CONNECTION            IP                SIGNAL  AUTOCONNECT");
  logger.info("%-10s  %-20s  %-16s  %6s  %s", stateName(ns.state()),
              up ? ns.currentSsid().c_str() : "--", up ? ns.localIp().c_str() : "--",
              up ? String(WiFi.RSSI()).c_str() : "--", ns.autoReconnect() ? "yes" : "no");
  if (up) {
    // Шлюз і DNS окремим рядком, як IP4.GATEWAY / IP4.DNS у nmcli: без них
    // "IP є, а нічого не працює" неможливо діагностувати з консолі.
    logger.info("IP4.GATEWAY %s   IP4.DNS %s", WiFi.gatewayIP().toString().c_str(),
                WiFi.dnsIP().toString().c_str());
  }
  logger.info("%u saved profile(s)", (unsigned)ns.connections().size());
}

static void objectGeneral(NetworkSupervisor& ns, const std::vector<String>& args) {
  static const char* const verbs[] = {"status", "help"};
  if (args.size() < 2) {
    generalStatus(ns);
    return;
  }
  const int v = match(args[1], verbs, 2);
  if (v < 0) {
    reportMatch(v, args[1], verbs, 2);
    return;
  }
  if (v == 1) {
    printHelp();
    return;
  }
  generalStatus(ns);
}

// ---------------------------------------------------------------------------
// radio
// ---------------------------------------------------------------------------

static void objectRadio(NetworkSupervisor& ns, const std::vector<String>& args) {
  if (args.size() < 2 || args[1].equalsIgnoreCase("help")) {
    logger.info("Usage: net radio wifi [on|off]");
    return;
  }
  static const char* const verbs[] = {"wifi", "help"};
  const int v = match(args[1], verbs, 2);
  if (v < 0) {
    reportMatch(v, args[1], verbs, 2);
    return;
  }
  if (v == 1) {
    logger.info("Usage: net radio wifi [on|off]");
    return;
  }

  if (args.size() < 3) {
    logger.info("WIFI: %s", ns.state() == NetworkSupervisorState::IDLE ? "disabled" : "enabled");
    return;
  }

  bool on = false;
  if (!parseBool(args[2], on)) {
    logger.warn("use: net radio wifi on|off");
    return;
  }
  if (on) {
    ns.begin();  // no-op, якщо FSM уже не в IDLE
    logger.info("WIFI: enabled");
  } else {
    ns.end();
    logger.info("WIFI: disabled");
  }
}

// ---------------------------------------------------------------------------
// device
// ---------------------------------------------------------------------------

static void deviceStatus(NetworkSupervisor& ns) {
  logger.info("DEVICE  TYPE  STATE       CONNECTION");
  logger.info("wlan0   wifi  %-10s  %s", stateName(ns.state()),
              ns.isConnected() ? ns.currentSsid().c_str() : "--");
  logger.info("MAC %s", WiFi.macAddress().c_str());
}

static void deviceWifi(NetworkSupervisor& ns, const std::vector<String>& args) {
  static const char* const verbs[] = {"list", "connect", "hotspot", "rescan", "help"};
  if (args.size() < 3) {
    WiFi_scan(knownSsids(ns));  // 'net device wifi' == list, як у nmcli
    return;
  }
  const int v = match(args[2], verbs, 5);
  if (v < 0) {
    reportMatch(v, args[2], verbs, 5);
    return;
  }

  switch (v) {
    case 0:    // list
    case 3: {  // rescan
      if (ns.state() == NetworkSupervisorState::SCANNING) {
        logger.warn("supervisor is scanning right now, try again in a moment");
        return;
      }
      WiFi_scan(knownSsids(ns));
      return;
    }
    case 1: {  // connect
      if (args.size() < 4) {
        logger.warn("use: net device wifi connect <ssid> [password <p>]");
        return;
      }
      const String targetSsid = args[3];
      const String pass = namedArg(args, 4, "password");

      // Семантика nmcli: connect створює профіль, якщо його ще немає,
      // і оновлює пароль, якщо він уже є.
      WifiConnection* existing = nullptr;
      for (const auto& c : ns.connections()) {
        if (targetSsid == c.ssid.c_str()) {
          existing = ns.getConnection(c.connectionId);
          break;
        }
      }

      uint16_t id;
      if (existing) {
        if (pass.length()) existing->password = pass.c_str();
        existing->isEnabled = true;
        id = existing->connectionId;
      } else {
        WifiConnection conn;
        conn.ssid = targetSsid.c_str();
        conn.password = pass.c_str();
        id = ns.addConnection(conn);
      }
      ns.saveConfig();

      // Явний connect скасовує попередній ручний disconnect - інакше FSM
      // підключився б і одразу лишився без нагляду.
      ns.setAutoReconnect(true);

      if (ns.connectTo(id)) {
        logger.info("connecting to '%s' (profile %u)", targetSsid.c_str(), (unsigned)id);
      } else {
        logger.warn("profile %u vanished", (unsigned)id);
      }
      return;
    }
    case 2: {  // hotspot
      NetworkSupervisorConfig cfg = ns.config();
      bool hasSsid = false, hasPass = false;
      const String apSsid = namedArg(args, 3, "ssid", &hasSsid);
      const String apPass = namedArg(args, 3, "password", &hasPass);
      if (hasSsid) cfg.apSsid = apSsid.c_str();
      if (hasPass) cfg.apPassword = apPass.c_str();
      if (hasSsid || hasPass) {
        ns.setConfig(cfg);
        ns.saveConfig();
      }
      ns.startAp();
      logger.info("hotspot '%s' at %s", cfg.apSsid.c_str(), cfg.apIp.c_str());
      return;
    }
    default:
      logger.info("Usage: net device wifi {list|connect|hotspot|rescan}");
      return;
  }
}

static void objectDevice(NetworkSupervisor& ns, const std::vector<String>& args) {
  static const char* const verbs[] = {"status", "wifi", "disconnect", "help"};
  if (args.size() < 2) {
    deviceStatus(ns);
    return;
  }
  const int v = match(args[1], verbs, 4);
  if (v < 0) {
    reportMatch(v, args[1], verbs, 4);
    return;
  }
  switch (v) {
    case 0:
      deviceStatus(ns);
      return;
    case 1:
      deviceWifi(ns, args);
      return;
    case 2:
      // nmcli: ручний disconnect глушить і автопідключення, інакше менеджер
      // тут же підняв би зʼєднання назад.
      ns.setAutoReconnect(false);
      WiFi.disconnect(true);
      logger.info("device disconnected, autoconnect off");
      return;
    default:
      logger.info("Usage: net device {status|wifi|disconnect}");
      return;
  }
}

// ---------------------------------------------------------------------------
// connection
// ---------------------------------------------------------------------------

static void connectionShowAll(NetworkSupervisor& ns) {
  const auto& list = ns.connections();
  if (list.empty()) {
    logger.warn("no saved profiles");
    return;
  }
  const String active = ns.isConnected() ? String(ns.currentSsid().c_str()) : String("");
  logger.info("ID  SSID                              PRIO  AUTOCONNECT  SIGNAL  ACTIVE");
  for (const auto& c : list) {
    logger.info("%-2u  %-32s  %4d  %-11s  %6s  %s", (unsigned)c.connectionId, c.ssid.c_str(),
                (int)c.priority, c.isEnabled ? "yes" : "no",
                c.rssi ? String((int)c.rssi).c_str() : "--",
                (active.length() && active == c.ssid.c_str()) ? "yes" : "no");
  }
}

static void connectionShowOne(NetworkSupervisor& ns, const WifiConnection& c) {
  logger.info("connection.id:                    %u", (unsigned)c.connectionId);
  logger.info("wifi.ssid:                        %s", c.ssid.c_str());
  logger.info("wifi-security.psk:                %s", c.password.empty() ? "--" : "(set)");
  logger.info("connection.autoconnect:           %s", c.isEnabled ? "yes" : "no");
  logger.info("connection.autoconnect-priority:  %d", (int)c.priority);
  logger.info("connection.autoconnect-retries:   %d", (int)c.maxRetries);
  logger.info("connection.last-connected:        %u", (unsigned)c.lastConnected);
  logger.info("ipv4.method:                      %s", c.staticIp ? "manual" : "auto");
  if (c.staticIp) {
    logger.info("ipv4.addresses:                   %s / %s", c.ip.c_str(), c.subnet.c_str());
    logger.info("ipv4.gateway:                     %s", c.gateway.c_str());
    logger.info("ipv4.dns:                         %s", c.dns.c_str());
  }
  logger.info("signal:                           %d", (int)c.rssi);
  (void)ns;
}

// Маска підмережі з довжини префікса CIDR (ipv4.addresses = 192.168.1.50/24).
static String cidrToMask(int bits) {
  if (bits < 0 || bits > 32) bits = 24;
  const uint32_t mask = bits == 0 ? 0u : (0xFFFFFFFFu << (32 - bits));
  char buf[16];
  snprintf(buf, sizeof(buf), "%u.%u.%u.%u", (mask >> 24) & 0xFF, (mask >> 16) & 0xFF,
           (mask >> 8) & 0xFF, mask & 0xFF);
  return String(buf);
}

static void connectionModify(NetworkSupervisor& ns, WifiConnection& c, const String& setting,
                             const String& value) {
  // Повні nmcli-івські імена. Скорочення секції ('wifi-sec.psk', 'con.auto...')
  // ловляться тим самим match() - воно працює по префіксу.
  static const char* const settings[] = {
    "wifi.ssid",
    "wifi-security.psk",
    "connection.autoconnect",
    "connection.autoconnect-priority",
    "connection.autoconnect-retries",
    "ipv4.method",
    "ipv4.addresses",
    "ipv4.gateway",
    "ipv4.dns",
  };
  const size_t count = sizeof(settings) / sizeof(settings[0]);

  const int s = matchSetting(setting, settings, count);
  if (s < 0) {
    reportMatch(s, setting, settings, count);
    return;
  }

  switch (s) {
    case 0:
      c.ssid = value.c_str();
      break;
    case 1:
      c.password = value.c_str();
      break;
    case 2: {
      bool on = false;
      if (!parseBool(value, on)) {
        logger.warn("connection.autoconnect: expected yes|no");
        return;
      }
      c.isEnabled = on;
      break;
    }
    case 3:
      c.priority = static_cast<int8_t>(value.toInt());
      break;
    case 4:
      c.maxRetries = static_cast<int8_t>(value.toInt());
      break;
    case 5: {
      if (value.equalsIgnoreCase("manual")) {
        if (c.ip.empty()) {
          logger.warn("ipv4.method manual needs ipv4.addresses first");
          return;
        }
        c.staticIp = true;
      } else if (value.equalsIgnoreCase("auto")) {
        c.staticIp = false;
      } else {
        logger.warn("ipv4.method: expected auto|manual");
        return;
      }
      break;
    }
    case 6: {
      const int slash = value.indexOf('/');
      c.ip = (slash < 0 ? value : value.substring(0, slash)).c_str();
      c.subnet = cidrToMask(slash < 0 ? 24 : value.substring(slash + 1).toInt()).c_str();
      // Як у nmcli: задана адреса сама по собі ще не вмикає статику, але
      // тримати ipv4.addresses і при цьому method=auto - типова пастка,
      // тому вмикаємо одразу.
      c.staticIp = true;
      break;
    }
    case 7:
      c.gateway = value.c_str();
      break;
    case 8:
      c.dns = value.c_str();
      break;
    default:
      return;
  }

  ns.saveConfig();
  logger.info("profile %u: %s = %s", (unsigned)c.connectionId, settings[s], value.c_str());
}

static void objectConnection(NetworkSupervisor& ns, const std::vector<String>& args) {
  static const char* const verbs[] = {"show", "add",    "modify", "delete", "up",
                                      "down", "reload", "load",   "export", "help"};
  const size_t count = sizeof(verbs) / sizeof(verbs[0]);

  if (args.size() < 2) {
    connectionShowAll(ns);
    return;
  }
  const int v = match(args[1], verbs, count);
  if (v < 0) {
    reportMatch(v, args[1], verbs, count);
    return;
  }

  switch (v) {
    case 0: {  // show
      if (args.size() < 3) {
        connectionShowAll(ns);
        return;
      }
      WifiConnection* c = find(ns, args[2]);
      if (!c) {
        logger.warn("unknown profile '%s'", args[2].c_str());
        return;
      }
      connectionShowOne(ns, *c);
      return;
    }
    case 1: {  // add
      bool hasSsid = false;
      String newSsid = namedArg(args, 2, "ssid", &hasSsid);
      // Дозволяємо і позиційну форму 'net c add <ssid>' - вона коротша, а
      // плутанини не створює: інші аргументи тут завжди іменовані.
      if (!hasSsid && args.size() >= 3) newSsid = args[2];
      if (newSsid.length() == 0) {
        logger.warn("use: net connection add ssid <s> [password <p>] [priority <n>]");
        return;
      }
      bool hasPriority = false;
      const String pass = namedArg(args, 2, "password");
      const String prio = namedArg(args, 2, "priority", &hasPriority);

      WifiConnection conn;
      conn.ssid = newSsid.c_str();
      conn.password = pass.c_str();
      if (hasPriority) conn.priority = static_cast<int8_t>(prio.toInt());
      const uint16_t id = ns.addConnection(conn);
      ns.saveConfig();
      logger.info("profile %u added: '%s'", (unsigned)id, newSsid.c_str());
      return;
    }
    case 2: {  // modify
      if (args.size() < 5) {
        logger.warn("use: net connection modify <id|ssid> <setting> <value>");
        return;
      }
      WifiConnection* c = find(ns, args[2]);
      if (!c) {
        logger.warn("unknown profile '%s'", args[2].c_str());
        return;
      }
      connectionModify(ns, *c, args[3], args[4]);
      return;
    }
    case 3: {  // delete
      if (args.size() < 3) {
        logger.warn("use: net connection delete <id|ssid>");
        return;
      }
      WifiConnection* c = find(ns, args[2]);
      if (!c) {
        logger.warn("unknown profile '%s'", args[2].c_str());
        return;
      }
      const uint16_t id = c->connectionId;
      const String gone(c->ssid.c_str());
      ns.removeConnection(id);
      ns.saveConfig();
      logger.info("profile %u deleted: '%s'", (unsigned)id, gone.c_str());
      return;
    }
    case 4: {  // up
      if (args.size() < 3) {
        logger.warn("use: net connection up <id|ssid>");
        return;
      }
      WifiConnection* c = find(ns, args[2]);
      if (!c) {
        logger.warn("unknown profile '%s'", args[2].c_str());
        return;
      }
      // 'up' на вимкненому профілі мовчки нічого б не дав - вмикаємо явно,
      // як робить nmcli (він теж піднімає з'єднання попри autoconnect=no).
      if (!c->isEnabled) {
        c->isEnabled = true;
        ns.saveConfig();
      }
      ns.setAutoReconnect(true);
      const uint16_t id = c->connectionId;
      const String target(c->ssid.c_str());
      if (ns.connectTo(id)) {
        logger.info("activating profile %u: '%s'", (unsigned)id, target.c_str());
      } else {
        logger.warn("profile %u vanished", (unsigned)id);
      }
      return;
    }
    case 5:  // down
      ns.setAutoReconnect(false);
      WiFi.disconnect(true);
      logger.info("connection down, autoconnect off");
      return;
    case 6: {  // reload — як 'nmcli connection reload': перечитати файли з диска
      size_t added = 0;
      const size_t read = importDir(ns, &added);
      if (read == 0) {
        logger.warn("no *.nmconnection files in %s", kProfileDir);
        return;
      }
      ns.saveConfig();
      logger.info("%u profile(s) read from %s (%u new, %u updated)", (unsigned)read, kProfileDir,
                  (unsigned)added, (unsigned)(read - added));
      return;
    }
    case 7: {  // load <file>
      if (args.size() < 3) {
        logger.warn("use: net connection load <file>");
        return;
      }
      String path = args[2];
      if (!path.startsWith("/")) path = String(kProfileDir) + "/" + path;
      if (!importFile(ns, path)) return;
      ns.saveConfig();
      logger.info("loaded %s", path.c_str());
      return;
    }
    case 8: {  // export [<id|ssid>]
      if (args.size() >= 3) {
        WifiConnection* c = find(ns, args[2]);
        if (!c) {
          logger.warn("unknown profile '%s'", args[2].c_str());
          return;
        }
        const String path = exportOne(*c);
        if (path.length()) logger.info("wrote %s", path.c_str());
        return;
      }
      size_t written = 0;
      for (const auto& c : ns.connections()) {
        if (exportOne(c).length()) ++written;
      }
      logger.info("%u of %u profile(s) written to %s", (unsigned)written,
                  (unsigned)ns.connections().size(), kProfileDir);
      return;
    }
    default:
      printHelp();
      return;
  }
}

// ---------------------------------------------------------------------------
// Точка входу
// ---------------------------------------------------------------------------

static void dispatch(NetworkSupervisor& ns, const String& rawArgs) {
  std::vector<String> args;
  splitArgs(rawArgs, args);

  if (args.empty()) {
    generalStatus(ns);
    return;
  }

  static const char* const objects[] = {"general", "radio", "device", "connection", "help"};
  const int o = match(args[0], objects, 5);
  if (o < 0) {
    reportMatch(o, args[0], objects, 5);
    return;
  }

  switch (o) {
    case 0: objectGeneral(ns, args); return;
    case 1: objectRadio(ns, args); return;
    case 2: objectDevice(ns, args); return;
    case 3: objectConnection(ns, args); return;
    default: printHelp(); return;
  }
}

}  // namespace netcli

// Засів списку профілів файлами з LittleFS. Викликати ПІСЛЯ ns.loadConfig().
// Повертає кількість прочитаних профілів (0 — каталогу немає або він порожній).
inline size_t importNetProfilesFromFs(NetworkSupervisor& ns) {
  const size_t read = netcli::importDir(ns);
  if (read) ns.saveConfig();
  return read;
}

inline void registerNetCommand(SerialCommander& commander, NetworkSupervisor& ns) {
  commander.registerCommand(
    "net", "nmcli-style wifi manager: net {general|radio|device|connection} ... ('net help')",
    [&ns](const String& args) { netcli::dispatch(ns, args); });
}
