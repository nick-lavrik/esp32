#include "NetworkSupervisor.hpp"

#include <ArduinoJson.h>

#include <TLogger.hpp>
#include <algorithm>

#include "ConfigStorage.hpp"

// Портабельна затримка: delay() на ESP8266/blocking, vTaskDelay() на ESP32
#if defined(NM_BLOCKING_MODE) || defined(ESP8266)
#define NM_DELAY(ms) delay(ms)
#else
#define NM_DELAY(ms) vTaskDelay(pdMS_TO_TICKS(ms))
#endif

// Тег "net" - той самий, під яким світяться команди netcli, щоб грепати лог
// мережі одним фільтром.
static const TLogger _logger{"net"};

static constexpr char kConfigKey[] = "nm_config";
static constexpr char kConnectionsKey[] = "nm_conn";
static constexpr char kIdCounterKey[] = "nm_id_ctr";

// ============================================================
// Ctor / Dtor
// ============================================================

NetworkSupervisor::NetworkSupervisor(ConfigStorage* storage) : _storage(storage) {
#if !defined(ESP8266)
  _mutex = xSemaphoreCreateRecursiveMutex();
#endif
}

NetworkSupervisor::~NetworkSupervisor() {
  end();
#if !defined(ESP8266)
  if (_mutex) {
    vSemaphoreDelete(_mutex);
    _mutex = nullptr;
  }
#endif
}

// ============================================================
// Ініціалізація
// ============================================================

void NetworkSupervisor::begin() {
  if (_state != NetworkSupervisorState::IDLE) return;
  _autoReconnect = _config.autoReconnect;

#if !defined(NM_BLOCKING_MODE) && !defined(ESP8266)
  // В IDLE можна опинитись не тільки до першого begin(), а й після ручного
  // disconnect - task при цьому живий і події зареєстровані. Другий
  // xTaskCreate() дав би два FSM на одне радіо.
  if (_taskHandle) {
    _setState(NetworkSupervisorState::SCANNING);
    return;
  }
#endif

  _registerWifiEvents();

  // Мережевий стек піднімаємо ТУТ, у контексті того, хто викликав begin(), і
  // тільки потім стартуємо task.
  //
  // Інакше перший WiFi.mode() робить уже FSM-task - паралельно з рештою
  // setup(). А esp_netif_init() всередині не потокобезпечний: обидва
  // контексти бачать стек неініціалізованим, обидва кличуть
  // esp_vfs_lwip_sockets_register(), другий отримує ESP_ERR_INVALID_ARG - і
  // ESP_ERROR_CHECK всередині IDF валить пристрій у бут-луп
  // (vfs_lwip.c:112). Раніше цієї гонки не було: setupWiFi() робив
  // WiFi.mode() синхронно, до всього іншого.
  _applyStaRadioConfig(/*keepAp=*/false);

  _setState(NetworkSupervisorState::SCANNING);
#if !defined(NM_BLOCKING_MODE) && !defined(ESP8266)
  xTaskCreate(_taskEntry, "NetworkSupervisor", 4096, this, 1, &_taskHandle);
#endif
}

void NetworkSupervisor::end() {
  _unregisterWifiEvents();
#if !defined(NM_BLOCKING_MODE) && !defined(ESP8266)
  if (_taskHandle) {
    vTaskDelete(_taskHandle);
    _taskHandle = nullptr;
  }
#endif
  if (_state == NetworkSupervisorState::AP_MODE) {
    WiFi.softAPdisconnect(true);
    _notifyApStopped();
  }
  WiFi.disconnect(true);
  _setState(NetworkSupervisorState::IDLE);
}

// ============================================================
// З'єднання
// ============================================================

uint16_t NetworkSupervisor::addConnection(const WifiConnection& conn) {
  Lock lock(_mutex);
  WifiConnection c = conn;
  c.connectionId = _nextConnectionId++;
  _connections.push_back(c);
  return c.connectionId;
}

bool NetworkSupervisor::removeConnection(uint16_t connectionId) {
  Lock lock(_mutex);
  for (auto it = _connections.begin(); it != _connections.end(); ++it) {
    if (it->connectionId == connectionId) {
      _connections.erase(it);
      if (_forcedId == connectionId) _forcedId = 0;
      return true;
    }
  }
  return false;
}

WifiConnection* NetworkSupervisor::getConnection(uint16_t connectionId) {
  Lock lock(_mutex);
  for (auto& c : _connections) {
    if (c.connectionId == connectionId) return &c;
  }
  return nullptr;
}

const std::vector<WifiConnection>& NetworkSupervisor::connections() const { return _connections; }

// ============================================================
// Стан
// ============================================================

NetworkSupervisorState NetworkSupervisor::state() const { return _state; }

bool NetworkSupervisor::isConnected() const { return _state == NetworkSupervisorState::CONNECTED; }

std::string NetworkSupervisor::currentSsid() const { return _currentSsid; }

std::string NetworkSupervisor::localIp() const { return _currentIp; }

// ============================================================
// Управління
// ============================================================

void NetworkSupervisor::setAutoReconnect(bool enabled) {
  _autoReconnect = enabled;
  _config.autoReconnect = enabled;
}

bool NetworkSupervisor::autoReconnect() const { return _autoReconnect; }

void NetworkSupervisor::_dropConnection(NetworkSupervisorState next) {
  const bool wasConnected = (_state == NetworkSupervisorState::CONNECTED);
  const std::string lost = _currentSsid;
  _currentSsid.clear();
  _currentIp.clear();

  // Стан знімаємо ДО disconnect(): подія STA_DISCONNECTED прилетить у вже
  // не-CONNECTED стан, тож про втрату звʼязку відзвітує рівно один шлях.
  // Інакше listeners отримували onDisconnected двічі - вдруге з порожнім
  // SSID, бо _currentSsid на той момент уже почищено, - а ще хендлер міг
  // перебити щойно виставлений нами стан своїм RECONNECTING.
  _setState(next);
  WiFi.disconnect(true);

  if (wasConnected) _notifyDisconnected(lost);
}

void NetworkSupervisor::reconnect() {
  _forcedId = 0;
  _candidateIndex = 0;
  _currentRetries = 0;
  _dropConnection(NetworkSupervisorState::SCANNING);
}

bool NetworkSupervisor::connectTo(uint16_t connectionId) {
  {
    // Замок тримаємо рівно на час перегляду вектора: нижче йдуть блокуючі
    // виклики WiFi.*, а під замком їх робити не можна (див. коментар до
    // _mutex у заголовку).
    Lock lock(_mutex);
    bool found = false;
    for (const auto& c : _connections) {
      if (c.connectionId == connectionId) {
        found = true;
        break;
      }
    }
    if (!found) return false;
  }

  if (_state == NetworkSupervisorState::AP_MODE) {
    WiFi.softAPdisconnect(true);
    _notifyApStopped();
  }

  _forcedId = connectionId;
  _candidateIndex = 0;
  _currentRetries = 0;
  _dropConnection(NetworkSupervisorState::CONNECTING);
  return true;
}

void NetworkSupervisor::startAp() {
  // Повторний виклик у вже піднятому AP тільки перезапустив би точку доступу
  // і вдруге вистрелив onApStarted - для команди "hotspot" це не те, чого
  // очікує користувач.
  if (_state == NetworkSupervisorState::AP_MODE) return;
  if (_state == NetworkSupervisorState::CONNECTED) {
    _dropConnection(NetworkSupervisorState::STARTING_AP);
  }
  _startApInternal();
}

void NetworkSupervisor::stopAp() {
  if (_state != NetworkSupervisorState::AP_MODE) return;
  WiFi.softAPdisconnect(true);
  _notifyApStopped();
  if (_autoReconnect) {
    _candidateIndex = 0;
    _currentRetries = 0;
    _setState(NetworkSupervisorState::SCANNING);
  } else {
    _setState(NetworkSupervisorState::IDLE);
  }
}

void NetworkSupervisor::scan() {
  // FSM уже сканує - другий одночасний скан однаково поверне
  // WIFI_SCAN_FAILED, а результат першого зіпсує собі ж.
  if (_state == NetworkSupervisorState::SCANNING) return;

  const bool keepAp = (_state == NetworkSupervisorState::AP_MODE);
  _notifyScanStart();
  _applyStaRadioConfig(keepAp);
  _runScan(keepAp);  // блокуючий: викликається з контексту команди, не з FSM

  auto known = _sortedCandidates();
  _notifyScanResult(known);
  _notifyScanEnd(known);
}

void NetworkSupervisor::startWps() {
  if (_state == NetworkSupervisorState::AP_MODE) {
    WiFi.softAPdisconnect(true);
    _notifyApStopped();
  } else if (_state == NetworkSupervisorState::CONNECTED) {
    _dropConnection(NetworkSupervisorState::WPS_WAITING);
  }
  _startWpsInternal();
}

// ============================================================
// Конфігурація
// ============================================================

void NetworkSupervisor::setConfig(const NetworkSupervisorConfig& cfg) {
  _config = cfg;
  _autoReconnect = cfg.autoReconnect;
}

const NetworkSupervisorConfig& NetworkSupervisor::config() const { return _config; }

void NetworkSupervisor::setClock(std::function<uint32_t()> nowEpoch) { _clock = std::move(nowEpoch); }

uint32_t NetworkSupervisor::_now() const {
  if (_clock) {
    uint32_t epoch = _clock();
    if (epoch > 0) return epoch;
  }
  // Фолбек: грубий лічильник аптайму. Він менший за будь-який unix-час, тому
  // запис, підключений до синхронізації часу, ніколи не перебʼє свіжіший.
  return static_cast<uint32_t>(millis() / 1000);
}

void NetworkSupervisor::saveConfig() {
  if (!_storage) return;
  Lock lock(_mutex);

  // --- config ---
  {
    JsonDocument doc;
    doc["scanIntervalMs"] = _config.scanIntervalMs;
    doc["connectTimeoutMs"] = _config.connectTimeoutMs;
    doc["retryDelayMs"] = _config.retryDelayMs;
    doc["maxRetries"] = _config.maxRetries;
    doc["autoReconnect"] = _config.autoReconnect;
    doc["scanBeforeConnect"] = _config.scanBeforeConnect;
    doc["forceWifi4"] = _config.forceWifi4;
    doc["fullChannelScan"] = _config.fullChannelScan;
    doc["apFallbackEnabled"] = _config.apFallbackEnabled;
    doc["apSsid"] = _config.apSsid.c_str();
    doc["apPassword"] = _config.apPassword.c_str();
    doc["apChannel"] = _config.apChannel;
    doc["apIp"] = _config.apIp.c_str();
    doc["wpsEnabled"] = _config.wpsEnabled;
    doc["wpsMethod"] = static_cast<uint8_t>(_config.wpsMethod);
    doc["wpsTimeoutMs"] = _config.wpsTimeoutMs;
    doc["wpsPin"] = _config.wpsPin.c_str();
    doc["wpsSaveOnSuccess"] = _config.wpsSaveOnSuccess;
    doc["wpsSavedPriority"] = _config.wpsSavedPriority;

    String json;
    serializeJson(doc, json);
    _storage->setString(kConfigKey, json);
  }

  // --- connections ---
  {
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    for (const auto& c : _connections) {
      JsonObject obj = arr.add<JsonObject>();
      obj["id"] = c.connectionId;
      obj["ssid"] = c.ssid.c_str();
      obj["password"] = c.password.c_str();
      obj["priority"] = c.priority;
      obj["lastConnected"] = c.lastConnected;
      obj["isEnabled"] = c.isEnabled;
      obj["maxRetries"] = c.maxRetries;
      obj["staticIp"] = c.staticIp;
      if (c.staticIp) {
        obj["ip"] = c.ip.c_str();
        obj["gateway"] = c.gateway.c_str();
        obj["subnet"] = c.subnet.c_str();
        obj["dns"] = c.dns.c_str();
      }
    }
    String json;
    serializeJson(doc, json);
    _storage->setString(kConnectionsKey, json);
  }

  // --- id counter ---
  _storage->setInt(kIdCounterKey, _nextConnectionId);
}

void NetworkSupervisor::loadConfig() {
  if (!_storage) return;
  Lock lock(_mutex);

  // --- config ---
  {
    String json = _storage->getString(kConfigKey, "");
    if (json.length() > 0) {
      JsonDocument doc;
      if (deserializeJson(doc, json) == DeserializationError::Ok) {
        _config.scanIntervalMs = doc["scanIntervalMs"] | _config.scanIntervalMs;
        _config.connectTimeoutMs = doc["connectTimeoutMs"] | _config.connectTimeoutMs;
        _config.retryDelayMs = doc["retryDelayMs"] | _config.retryDelayMs;
        _config.maxRetries = doc["maxRetries"] | _config.maxRetries;
        _config.autoReconnect = doc["autoReconnect"] | _config.autoReconnect;
        _config.scanBeforeConnect = doc["scanBeforeConnect"] | _config.scanBeforeConnect;
        _config.forceWifi4 = doc["forceWifi4"] | _config.forceWifi4;
        _config.fullChannelScan = doc["fullChannelScan"] | _config.fullChannelScan;
        _config.apFallbackEnabled = doc["apFallbackEnabled"] | _config.apFallbackEnabled;
        if (doc["apSsid"].is<const char*>()) _config.apSsid = doc["apSsid"].as<const char*>();
        if (doc["apPassword"].is<const char*>())
          _config.apPassword = doc["apPassword"].as<const char*>();
        _config.apChannel = doc["apChannel"] | _config.apChannel;
        if (doc["apIp"].is<const char*>()) _config.apIp = doc["apIp"].as<const char*>();
        _config.wpsEnabled = doc["wpsEnabled"] | _config.wpsEnabled;
        _config.wpsMethod =
          static_cast<WpsMethod>(doc["wpsMethod"] | static_cast<uint8_t>(_config.wpsMethod));
        _config.wpsTimeoutMs = doc["wpsTimeoutMs"] | _config.wpsTimeoutMs;
        if (doc["wpsPin"].is<const char*>()) _config.wpsPin = doc["wpsPin"].as<const char*>();
        _config.wpsSaveOnSuccess = doc["wpsSaveOnSuccess"] | _config.wpsSaveOnSuccess;
        _config.wpsSavedPriority = doc["wpsSavedPriority"] | _config.wpsSavedPriority;
        _autoReconnect = _config.autoReconnect;
      }
    }
  }

  // --- connections ---
  {
    String json = _storage->getString(kConnectionsKey, "");
    if (json.length() > 0) {
      JsonDocument doc;
      if (deserializeJson(doc, json) == DeserializationError::Ok && doc.is<JsonArray>()) {
        _connections.clear();
        for (JsonObject obj : doc.as<JsonArray>()) {
          WifiConnection c;
          c.connectionId = obj["id"] | static_cast<uint16_t>(0);
          if (obj["ssid"].is<const char*>()) c.ssid = obj["ssid"].as<const char*>();
          if (obj["password"].is<const char*>()) c.password = obj["password"].as<const char*>();
          c.priority = obj["priority"] | static_cast<int8_t>(0);
          c.lastConnected = obj["lastConnected"] | static_cast<uint32_t>(0);
          c.isEnabled = obj["isEnabled"] | true;
          c.maxRetries = obj["maxRetries"] | static_cast<int8_t>(-1);
          c.staticIp = obj["staticIp"] | false;
          if (c.staticIp) {
            if (obj["ip"].is<const char*>()) c.ip = obj["ip"].as<const char*>();
            if (obj["gateway"].is<const char*>()) c.gateway = obj["gateway"].as<const char*>();
            if (obj["subnet"].is<const char*>()) c.subnet = obj["subnet"].as<const char*>();
            if (obj["dns"].is<const char*>()) c.dns = obj["dns"].as<const char*>();
          }
          _connections.push_back(c);
        }
      }
    }
  }

  // --- id counter ---
  _nextConnectionId = static_cast<uint16_t>(_storage->getInt(kIdCounterKey, 1));
}

// ============================================================
// Listeners
// ============================================================

NsListenerId NetworkSupervisor::addListener(INetworkSupervisorListener* listener) {
  if (!listener) return kInvalidNsListenerId;
  NsListenerId id = _nextListenerId++;
  _listeners.push_back({id, listener});
  return id;
}

void NetworkSupervisor::removeListener(NsListenerId id) {
  _listeners.erase(std::remove_if(_listeners.begin(), _listeners.end(),
                                  [id](const ListenerEntry& e) { return e.id == id; }),
                   _listeners.end());
}

// ============================================================
// FSM — внутрішня логіка
// ============================================================

void NetworkSupervisor::_setState(NetworkSupervisorState next) { _state = next; }

void NetworkSupervisor::_applyStaRadioConfig(bool keepAp) {
  // keepAp: AP_STA тримає точку доступу піднятою, поки станція сканує ефір.
  WiFi.mode(keepAp ? WIFI_AP_STA : WIFI_STA);

  // Вимикаємо ВЛАСНИЙ автореконект ядра Arduino: рішення про перепідключення
  // тут ухвалює лише FSM. З увімкненим - ядро мовчки переасоціюється до
  // останньої точки, і це ламає одразу дві речі: "net device disconnect" не
  // тримається (станція вертається сама), а профіль, вимкнений через
  // connection.autoconnect=no, все одно піднімається. Гірший наслідок - стан
  // WL_CONNECTED без IP: FSM вважає, що все гаразд, а DNS не резолвиться.
  WiFi.setAutoReconnect(false);

  // УВАГА: тут НЕ "#if ESP32". arduino-esp32 визначає цей макрос самопосилально
  // (-DESP32=ESP32, див. pioarduino-build.py), а такий ідентифікатор у #if
  // розкривається сам у себе й дає 0. Перевірка на ESP8266 - єдина правильна:
  // esp_wifi.h є в усій родині ESP32.
#if !defined(ESP8266)
  if (_config.forceWifi4) {
    // Обмежуємо станцію 802.11b/g/n ДО старту конекту: на чипах з Wi-Fi 6
    // (C6) асоціація з AX-точкою валила стек. На чипах без AX (класичний
    // ESP32, S3, C3) це фактично no-op - там така бітмаска і так дефолтна, -
    // але тримаємо однаково для всіх, щоб поведінка не залежала від плати.
    esp_err_t err = esp_wifi_set_protocol(
      WIFI_IF_STA, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N);
    if (err != ESP_OK) {
      _logger.warn("802.11n protocol change failed: %d", err);
    }
  }

  if (_config.fullChannelScan) {
    // Повний скан по всіх каналах замість дефолтного WIFI_FAST_SCAN.
    //
    // Fast scan зупиняється на ПЕРШІЙ точці з потрібним SSID і слухає кожен
    // канал дуже коротко - при слабкому сигналі beacon просто не встигає
    // потрапити у вікно, і WiFi.begin() віддає reason 201 (NO_AP_FOUND) на
    // мережу, яку окремий WiFi.scanNetworks() бачить без проблем (той слухає
    // канал довше). Повний скан цю гонку прибирає.
    //
    // Побічний плюс для AiMesh/кількох AP з одним SSID: sort by signal
    // обирає найсильніший BSSID, а не перший-ліпший.
    WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);
    WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);
  }
#endif
}

std::vector<WifiConnection*> NetworkSupervisor::_sortedCandidates() {
  Lock lock(_mutex);
  std::vector<WifiConnection*> result;
  for (auto& c : _connections) {
    if (c.isEnabled) result.push_back(&c);
  }
  // Сортування: спочатку найновіший lastConnected, потім за priority DESC
  std::sort(result.begin(), result.end(), [](const WifiConnection* a, const WifiConnection* b) {
    if (a->lastConnected != b->lastConnected) return a->lastConnected > b->lastConnected;
    return a->priority > b->priority;
  });
  return result;
}

void NetworkSupervisor::_applyScanResults() {
  int16_t n = WiFi.scanComplete();
  if (n <= 0) return;

  Lock lock(_mutex);

  for (auto& c : _connections) c.rssi = 0;  // скидаємо rssi

  for (int16_t i = 0; i < n; ++i) {
    String scannedSsid = WiFi.SSID(i);
    int32_t rssi = WiFi.RSSI(i);
    for (auto& c : _connections) {
      if (c.ssid == scannedSsid.c_str()) {
        c.rssi = static_cast<int8_t>(rssi < -128 ? -128 : rssi);
        break;
      }
    }
  }
  WiFi.scanDelete();
}

bool NetworkSupervisor::_connectTo(WifiConnection& conn) {
  // Радіо треба переналаштувати перед КОЖНИМ begin(): після AP-циклу mode
  // лишається AP, і без цього вийшов би AP_STA замість STA.
  _applyStaRadioConfig(/*keepAp=*/false);
  _applyIpConfig(conn);

  WiFi.begin(conn.ssid.c_str(), conn.password.c_str());

  uint32_t deadline = millis() + _config.connectTimeoutMs;
  while (WiFi.status() != WL_CONNECTED && millis() < deadline) {
NM_DELAY(100);
  }

  if (WiFi.status() == WL_CONNECTED) {
    if (!conn.staticIp) _ensureDnsAfterDhcp();
    conn.lastConnected = _now();
    _currentSsid = conn.ssid;
    _currentIp = WiFi.localIP().toString().c_str();
    return true;
  }

  WiFi.disconnect(true);
  return false;
}

void NetworkSupervisor::_ensureDnsAfterDhcp() {
#if !defined(ESP8266)
  const IPAddress dns = WiFi.dnsIP();
  if (static_cast<uint32_t>(dns) != 0) return;  // DHCP усе віддав, нічого робити

  // DNS порожній попри отриману адресу. Причина в ядрі: STA.begin() (його
  // смикає будь-яка зміна WiFi.mode(), зокрема повернення з AP) викликає
  // NetworkInterface::config() без аргументів, а та гілка записує всі три
  // DNS-сервери нулями перед рестартом dhcpc. Клієнт при цьому переукладає
  // оренду з кешу і DNS назад уже не проставляє.
  //
  // Симптом назовні: IP є, LAN пінгується, а hostByName() падає з -54 -
  // тобто MQTT/NTP/SMTP мовчки не працюють. Підставляємо шлюз: на домашніх
  // роутерах саме він і роздається як резолвер по DHCP.
  const IPAddress gw = WiFi.gatewayIP();
  if (static_cast<uint32_t>(gw) == 0) {
    _logger.warn("DHCP left DNS empty and there is no gateway to fall back to");
    return;
  }

  // САМЕ WiFi.STA.dnsIP(), а НЕ WiFi.setDNS(). Останній розгортається в
  // STA.begin() -> connect() -> config(), тобто (а) знову обнуляє DNS перед
  // тим як його виставити і (б) переініціалізує netif. Пункт (б) коштував
  // бут-лупа: esp_vfs_lwip_sockets_register() вдруге віддає
  // ESP_ERR_INVALID_ARG, і ESP_ERROR_CHECK всередині IDF валить пристрій
  // (vfs_lwip.c:112). dnsIP() же лише кличе esp_netif_set_dns_info().
  if (WiFi.STA.dnsIP(0, gw)) {
    _logger.warn("DHCP left DNS empty, using gateway %s as resolver", gw.toString().c_str());
  } else {
    _logger.warn("DHCP left DNS empty and setting gateway %s as resolver failed",
                 gw.toString().c_str());
  }
#endif
}

void NetworkSupervisor::_applyIpConfig(const WifiConnection& conn) {
  if (conn.staticIp && !conn.ip.empty()) {
    IPAddress ip, gateway, subnet, dns;
    ip.fromString(conn.ip.c_str());
    gateway.fromString(conn.gateway.c_str());
    subnet.fromString(conn.subnet.c_str());
    dns.fromString(conn.dns.empty() ? "8.8.8.8" : conn.dns.c_str());
    WiFi.config(ip, gateway, subnet, dns);
    _staticIpApplied = true;
    return;
  }

  // DHCP. Скидати статику є сенс ЛИШЕ якщо ми її самі перед цим виставили.
  //
  // Безумовний виклик тут коштував робочого DNS: у ядрі гілка DHCP-клієнта
  // (NetworkInterface::config(), сюди приходить local_ip == INADDR_NONE)
  // спершу зупиняє dhcpc, потім записує ВСІ три DNS-сервери нулями і лише
  // тоді стартує dhcpc назад. Адресу пристрій отримує, а DNS лишається
  // 0.0.0.0 - назовні це виглядає як "IP є, пінг по LAN ходить, а
  // hostByName() падає з -54".
  if (!_staticIpApplied) return;

#if defined(ESP32)
  WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE);
#endif
  _staticIpApplied = false;
}

void NetworkSupervisor::_startApInternal() {
  _setState(NetworkSupervisorState::STARTING_AP);
  _forcedId = 0;

  WiFi.mode(WIFI_AP);
  IPAddress apIp;
  apIp.fromString(_config.apIp.c_str());
  WiFi.softAPConfig(apIp, apIp, IPAddress(255, 255, 255, 0));

  if (_config.apPassword.empty()) {
    WiFi.softAP(_config.apSsid.c_str(), nullptr, _config.apChannel);
  } else {
    WiFi.softAP(_config.apSsid.c_str(), _config.apPassword.c_str(), _config.apChannel);
  }

  _setState(NetworkSupervisorState::AP_MODE);
  _lastScanMs = millis();
  _notifyApStarted(_config.apSsid, _config.apIp);
}

void NetworkSupervisor::_runScan(bool keepAp) {
#if defined(ESP8266)
  (void)keepAp;
  WiFi.scanNetworks();  // синхронний, блокуючий ~2-3 сек
#else
  // show_hidden=false: приховані точки в результатах усе одно без SSID, тобто
  // зіставити їх зі збереженим записом неможливо - для них є connectTo().
  WiFi.scanNetworks(/*async=*/true);
  while (WiFi.scanComplete() == WIFI_SCAN_RUNNING) {
    vTaskDelay(pdMS_TO_TICKS(200));
  }
#endif
  _applyScanResults();
}

std::vector<WifiConnection*> NetworkSupervisor::_connectCandidates() {
  Lock lock(_mutex);  // тут лише читання вектора, жодних WiFi-викликів

  // connectTo(): б'ємось саме в цей запис, ігноруючи і пріоритети, і скан -
  // мережа може бути прихованою, тобто в результатах скану її не буде взагалі.
  if (_forcedId != 0) {
    std::vector<WifiConnection*> forced;
    for (auto& c : _connections) {
      if (c.connectionId == _forcedId) {
        forced.push_back(&c);
        break;
      }
    }
    return forced;
  }

  auto candidates = _sortedCandidates();
  if (_config.scanBeforeConnect) {
    // rssi == 0 - мережу не видно в ефірі (див. _applyScanResults())
    candidates.erase(std::remove_if(candidates.begin(), candidates.end(),
                                    [](const WifiConnection* c) { return c->rssi == 0; }),
                     candidates.end());
  }
  return candidates;
}

void NetworkSupervisor::_fallbackToApOrRescan() {
  if (_config.apFallbackEnabled) {
    _startApInternal();
    return;
  }
  // AP вимкнено - просто чекаємо наступного вікна сканування.
  _candidateIndex = 0;
  _currentRetries = 0;
  _forcedId = 0;
  _setState(NetworkSupervisorState::SCANNING);
  NM_DELAY(_config.scanIntervalMs);
}

// ============================================================
// FSM — основний цикл
// ============================================================

void NetworkSupervisor::_taskLoop() {
  while (true) {
    switch (_state) {
      // ----------------------------------------------------------
      case NetworkSupervisorState::SCANNING: {
        _notifyScanStart();

        _applyStaRadioConfig(/*keepAp=*/false);
        if (_config.scanBeforeConnect) _runScan(/*keepAp=*/false);

        auto candidates = _connectCandidates();

        _notifyScanResult(candidates);
        _notifyScanEnd(candidates);

        _candidateIndex = 0;
        _currentRetries = 0;

        if (candidates.empty()) {
          _fallbackToApOrRescan();
          break;
        }

        _setState(NetworkSupervisorState::CONNECTING);
        break;
      }

      // ----------------------------------------------------------
      case NetworkSupervisorState::CONNECTING: {
        auto candidates = _connectCandidates();

        if (_candidateIndex >= candidates.size()) {
          // всі кандидати вичерпано — WPS або AP fallback.
          // Форсована ціль (connectTo) на цьому знімається: далі знову
          // працює звичайний підбір за пріоритетами.
          _forcedId = 0;
          if (_config.wpsEnabled) {
            _startWpsInternal();
          } else {
            _fallbackToApOrRescan();
          }
          break;
        }

        WifiConnection* conn = candidates[_candidateIndex];
        _notifyConnecting(*conn);

        if (_connectTo(*conn)) {
          _forcedId = 0;
          _setState(NetworkSupervisorState::CONNECTED);
          _notifyConnected(*conn, _currentIp);
          _candidateIndex = 0;
          _currentRetries = 0;
        } else {
          uint8_t maxR =
              (conn->maxRetries >= 0) ? static_cast<uint8_t>(conn->maxRetries) : _config.maxRetries;
          _currentRetries++;
          if (_currentRetries >= maxR) {
            _notifyConnectionFailed(*conn);
            _candidateIndex++;
            _currentRetries = 0;
          }
NM_DELAY(_config.retryDelayMs);
        }
        break;
      }

      // ----------------------------------------------------------
      case NetworkSupervisorState::CONNECTED: {
        if (WiFi.status() != WL_CONNECTED) {
          std::string lost = _currentSsid;
          _currentSsid.clear();
          _currentIp.clear();
          _notifyDisconnected(lost);

          if (_autoReconnect) {
            _candidateIndex = 0;
            _currentRetries = 0;
            _setState(NetworkSupervisorState::RECONNECTING);
          } else {
            // Інакше стан лишався б CONNECTED назавжди, а ця гілка щопівсекунди
            // вистрелювала б onDisconnected заново - і при цьому state() брехав
            // би "connected" на вимкненому радіо.
            _setState(NetworkSupervisorState::IDLE);
          }
        }
NM_DELAY(500);
        break;
      }

      // ----------------------------------------------------------
      case NetworkSupervisorState::RECONNECTING: {
        // переходимо одразу на SCANNING для повного перебору
        _setState(NetworkSupervisorState::SCANNING);
        break;
      }

      // ----------------------------------------------------------
      case NetworkSupervisorState::WPS_WAITING: {
        if (_pollWpsResult()) {
          if (_wpsSuccess) {
            _onWpsSuccess(_wpsSsid, _wpsPassword);
          } else {
            // WPS fail/timeout — fallback до AP (або далі сканувати,
            // якщо apFallbackEnabled=false)
            _fallbackToApOrRescan();
          }
        } else {
NM_DELAY(200);
        }
        break;
      }

      // ----------------------------------------------------------
      case NetworkSupervisorState::AP_MODE: {
        if (!_autoReconnect) {
NM_DELAY(1000);
          break;
        }
        if (millis() - _lastScanMs < _config.scanIntervalMs) {
NM_DELAY(1000);
          break;
        }

        // Сканування У ФОНІ, не гасячи точку доступу: AP_STA дозволяє станції
        // пройтись по каналах, поки AP лишається в ефірі. Раніше тут був
        // softAPdisconnect() перед кожним скануванням - пристрій періодично
        // зникав з ефіру, і клієнт, підключений до AP, губив зʼєднання просто
        // тому, що настав час чергової перевірки.
        _lastScanMs = millis();

        if (_config.scanBeforeConnect) {
          _notifyScanStart();
          _applyStaRadioConfig(/*keepAp=*/true);
          _runScan(/*keepAp=*/true);

          auto visible = _connectCandidates();
          _notifyScanResult(visible);
          _notifyScanEnd(visible);

          if (visible.empty()) break;  // нема куди йти - лишаємось точкою доступу
        } else if (_sortedCandidates().empty()) {
          break;  // список порожній або все вимкнено - сканувати нема сенсу
        }

        // Відома мережа зʼявилась - тільки тепер згортаємо AP.
        WiFi.softAPdisconnect(true);
        _notifyApStopped();
        _candidateIndex = 0;
        _currentRetries = 0;
        _setState(NetworkSupervisorState::CONNECTING);
        break;
      }

      // ----------------------------------------------------------
      default:
NM_DELAY(100);
        break;
    }
  }
}

// ============================================================
// FreeRTOS task entry / blocking loop
// ============================================================

#if defined(NM_BLOCKING_MODE)
void NetworkSupervisor::loop() { _taskLoop(); }
#elif !defined(ESP8266)
void NetworkSupervisor::_taskEntry(void* param) {
  static_cast<NetworkSupervisor*>(param)->_taskLoop();
  vTaskDelete(nullptr);
}
#endif

// ============================================================
// Notify helpers
// ============================================================

void NetworkSupervisor::_notifyScanStart() {
  for (auto& e : _listeners) e.listener->onScanStart();
}

void NetworkSupervisor::_notifyScanResult(const std::vector<WifiConnection*>& known) {
  for (auto& e : _listeners) e.listener->onScanResult(known);
}

void NetworkSupervisor::_notifyScanEnd(const std::vector<WifiConnection*>& known) {
  for (auto& e : _listeners) e.listener->onScanEnd(known);
}

void NetworkSupervisor::_notifyConnecting(const WifiConnection& conn) {
  for (auto& e : _listeners) e.listener->onConnecting(conn);
}

void NetworkSupervisor::_notifyConnected(const WifiConnection& conn, const std::string& ip) {
  for (auto& e : _listeners) e.listener->onConnected(conn, ip);
}

void NetworkSupervisor::_notifyDisconnected(const std::string& ssid) {
  for (auto& e : _listeners) e.listener->onDisconnected(ssid);
}

void NetworkSupervisor::_notifyConnectionFailed(const WifiConnection& conn) {
  for (auto& e : _listeners) e.listener->onConnectionFailed(conn);
}

void NetworkSupervisor::_notifyApStarted(const std::string& apSsid, const std::string& ip) {
  for (auto& e : _listeners) e.listener->onApStarted(apSsid, ip);
}

void NetworkSupervisor::_notifyApStopped() {
  for (auto& e : _listeners) e.listener->onApStopped();
}

// ============================================================
// WPS — внутрішня логіка
// ============================================================

void NetworkSupervisor::_startWpsInternal() {
  _wpsSuccess = false;
  _wpsDone = false;
  _wpsSsid.clear();
  _wpsPassword.clear();
  _wpsStartMs = millis();
  _setState(NetworkSupervisorState::WPS_WAITING);

  uint8_t method = static_cast<uint8_t>(_config.wpsMethod);
  _notifyWpsStart(method);

#if defined(ESP8266)
  // ESP8266: тільки PBC, синхронний (блокуючий виклик ~2 хв).
  // Виконується в task, тому блокування прийнятне.
  bool ok = WiFi.beginWPSConfig();
  if (ok && WiFi.SSID().length() > 0) {
    _wpsSsid = WiFi.SSID().c_str();
    _wpsPassword = WiFi.psk().c_str();
    _wpsSuccess = true;
  }
  _wpsDone = true;
#else
  // ESP32: асинхронний через WiFi.onEvent() — результат прийде в event handler.
  // PIN mode: якщо wpsPin порожній — esp генерує PIN і стріляє ARDUINO_EVENT_WPS_ER_PIN.
  {
    esp_wps_config_t wpsConfig;
    memset(&wpsConfig, 0, sizeof(esp_wps_config_t));

    if (_config.wpsMethod == WpsMethod::PIN) {
      wpsConfig.wps_type = WPS_TYPE_PIN;
      // якщо wpsPin заданий — копіюємо в config.pin (8 символів)
      if (!_config.wpsPin.empty()) {
        snprintf(wpsConfig.pin, sizeof(wpsConfig.pin), "%s", _config.wpsPin.c_str());
      } else {
        snprintf(wpsConfig.pin, sizeof(wpsConfig.pin), "00000000");
      }
    } else {
      wpsConfig.wps_type = WPS_TYPE_PBC;
    }

    snprintf(wpsConfig.factory_info.manufacturer, sizeof(wpsConfig.factory_info.manufacturer),
             "ESPRESSIF");
    snprintf(wpsConfig.factory_info.model_name, sizeof(wpsConfig.factory_info.model_name),
             "ESPRESSIF IOT");
    snprintf(wpsConfig.factory_info.device_name, sizeof(wpsConfig.factory_info.device_name),
             "ESP32");

    esp_err_t err = esp_wifi_wps_enable(&wpsConfig);
    if (err != ESP_OK) return;

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
    esp_wifi_wps_start();
#else
    esp_wifi_wps_start(0);
#endif
  }
#endif
}

bool NetworkSupervisor::_pollWpsResult() {
#if defined(ESP8266)
  // ESP8266: _wpsDone встановлюється синхронно в _startWpsInternal()
  return _wpsDone;
#else
  // ESP32: _wpsDone встановлюється з WiFi.onEvent() handlers
  if (_wpsDone) return true;
  // перевіряємо таймаут (ESP32 event може не прийти при певних помилках)
  if (millis() - _wpsStartMs >= _config.wpsTimeoutMs) {
    esp_wifi_wps_disable();
    _notifyWpsTimeout();
    _wpsDone = true;
    _wpsSuccess = false;
    return true;
  }
  return false;
#endif
}

void NetworkSupervisor::_onWpsSuccess(const std::string& ssid, const std::string& password) {
  _notifyWpsSuccess(ssid, password);

  if (_config.wpsSaveOnSuccess && !ssid.empty()) {
    // перевіряємо чи мережа вже є в списку
    bool exists = false;
    for (auto& c : _connections) {
      if (c.ssid == ssid) {
        // оновлюємо пароль якщо змінився
        c.password = password;
        exists = true;
        break;
      }
    }
    if (!exists) {
      WifiConnection conn;
      conn.ssid = ssid;
      conn.password = password;
      conn.priority = _config.wpsSavedPriority;
      addConnection(conn);
    }
    saveConfig();
  }

  // підключаємось до щойно отриманої мережі
  _applyStaRadioConfig(/*keepAp=*/false);
  WiFi.begin(ssid.c_str(), password.c_str());
  uint32_t deadline = millis() + _config.connectTimeoutMs;
  while (WiFi.status() != WL_CONNECTED && millis() < deadline) {
NM_DELAY(100);
  }

  if (WiFi.status() == WL_CONNECTED) {
    _ensureDnsAfterDhcp();
    _currentSsid = ssid;
    _currentIp = WiFi.localIP().toString().c_str();
    // оновлюємо lastConnected
    for (auto& c : _connections) {
      if (c.ssid == ssid) {
        c.lastConnected = _now();
        break;
      }
    }
    _setState(NetworkSupervisorState::CONNECTED);
    // onConnected буде викликано з _evtStaGotIp handler (ESP32)
    // або тут (ESP8266/blocking)
#if defined(ESP8266) || defined(NM_BLOCKING_MODE)
    WifiConnection dummy;
    dummy.ssid = ssid;
    _notifyConnected(dummy, _currentIp);
#endif
  } else {
    WiFi.disconnect(true);
    _fallbackToApOrRescan();
  }
}

// ============================================================
// WiFi системні події — реєстрація / видалення
// ============================================================

// Хелпер: MAC-байти → "aa:bb:cc:dd:ee:ff"
static std::string macToStr(const uint8_t* mac) {
  char buf[18];
  snprintf(buf, sizeof(buf), "%02x:%02x:%02x:%02x:%02x:%02x",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return buf;
}

#if defined(ESP8266)
// ESP8266 має обмежений WiFiEventHandler API — реєструємо тільки доступні події
void NetworkSupervisor::_registerWifiEvents() {
  // На ESP8266 FSM покладається на polling (WiFi.status()), тому
  // системні події тільки прокидуємо в listeners де можливо.
  // StationConnectedHandler / StationDisconnectedHandler — не існують на ESP8266 STA.
  // Для AP доступні: onSoftAPModeStationConnected / Disconnected.
  _wifiApConnHandler = WiFi.onSoftAPModeStationConnected(
    [this](const WiFiEventSoftAPModeStationConnected& e) {
      _notifyApClientConnected(macToStr(e.mac));
    });
  _wifiApDisconnHandler = WiFi.onSoftAPModeStationDisconnected(
    [this](const WiFiEventSoftAPModeStationDisconnected& e) {
      _notifyApClientDisconnected(macToStr(e.mac));
    });
}

void NetworkSupervisor::_unregisterWifiEvents() {
  // WiFiEventHandler — scope-based, видаляються автоматично при присвоєнні порожнього
  _wifiApConnHandler = WiFiEventHandler{};
  _wifiApDisconnHandler = WiFiEventHandler{};
}

#else  // ESP32

void NetworkSupervisor::_registerWifiEvents() {
  // ---- STA ----
  _evtReady = WiFi.onEvent(
    [this](arduino_event_id_t, arduino_event_info_t) { _notifyWifiReady(); },
    ARDUINO_EVENT_WIFI_READY);

  _evtStaStart = WiFi.onEvent(
    [this](arduino_event_id_t, arduino_event_info_t) { _notifyStaStart(); },
    ARDUINO_EVENT_WIFI_STA_START);

  _evtStaStop = WiFi.onEvent(
    [this](arduino_event_id_t, arduino_event_info_t) { _notifyStaStop(); },
    ARDUINO_EVENT_WIFI_STA_STOP);

  _evtStaConnected = WiFi.onEvent(
    [this](arduino_event_id_t, arduino_event_info_t info) {
      // ssid може мати не-null-terminated символи — копіюємо безпечно
      char ssid[33] = {};
      memcpy(ssid, info.wifi_sta_connected.ssid,
             min((int)info.wifi_sta_connected.ssid_len, 32));
      _notifyStaConnected(ssid, info.wifi_sta_connected.channel);
    },
    ARDUINO_EVENT_WIFI_STA_CONNECTED);

  _evtStaGotIp = WiFi.onEvent(
    [this](arduino_event_id_t, arduino_event_info_t info) {
      // GOT_IP — підтверджуємо CONNECTED на рівні FSM
      _currentIp = IPAddress(info.got_ip.ip_info.ip.addr).toString().c_str();

      // Про вдале зʼєднання звітує ХТОСЬ ОДИН. Коли конект ініціював
      // _connectTo(), він сам вистрелить onConnected після свого polling-
      // циклу; якщо повідомити ще й звідси, listeners отримають подію двічі.
      // Тут лишається випадок, коли IP прийшов не з нашої ініціативи -
      // автореконект силами самого SDK.
      if (_state == NetworkSupervisorState::CONNECTED) return;

      // currentSsid вже встановлено в _connectTo(). Беремо КОПІЮ запису:
      // тримати замок під час _notifyConnected() не можна - listener живе в
      // прошивці й може робити що завгодно, зокрема викликати нас назад.
      WifiConnection snapshot;
      bool found = false;
      {
        Lock lock(_mutex);
        for (const auto& c : _connections) {
          if (c.ssid == _currentSsid) {
            snapshot = c;
            found = true;
            break;
          }
        }
      }
      if (found) {
        _setState(NetworkSupervisorState::CONNECTED);
        _notifyConnected(snapshot, _currentIp);
      }
    },
    ARDUINO_EVENT_WIFI_STA_GOT_IP);

  _evtStaLostIp = WiFi.onEvent(
    [this](arduino_event_id_t, arduino_event_info_t) { _notifyStaLostIp(); },
    ARDUINO_EVENT_WIFI_STA_LOST_IP);

  _evtStaDisconnected = WiFi.onEvent(
    [this](arduino_event_id_t, arduino_event_info_t) {
      // Без замка: тут лише скаляри й рядки стану, а блокування в хендлері
      // подій зупинило б увесь WiFi-стек (WiFi.mode()/softAP() чекають саме
      // на цей task).
      if (_state == NetworkSupervisorState::CONNECTED) {
        std::string lost = _currentSsid;
        _currentSsid.clear();
        _currentIp.clear();
        _notifyDisconnected(lost);
        if (_autoReconnect) {
          _candidateIndex = 0;
          _currentRetries = 0;
          _setState(NetworkSupervisorState::RECONNECTING);
        } else {
          _setState(NetworkSupervisorState::IDLE);
        }
      }
    },
    ARDUINO_EVENT_WIFI_STA_DISCONNECTED);

  _evtStaAuthMode = WiFi.onEvent(
    [this](arduino_event_id_t, arduino_event_info_t info) {
      _notifyStaAuthModeChanged(
        static_cast<uint8_t>(info.wifi_sta_authmode_change.old_mode),
        static_cast<uint8_t>(info.wifi_sta_authmode_change.new_mode));
    },
    ARDUINO_EVENT_WIFI_STA_AUTHMODE_CHANGE);

  _evtStaGotIp6 = WiFi.onEvent(
    [this](arduino_event_id_t, arduino_event_info_t info) {
      char buf[40] = {};
      // ip6_addr_t → рядок
      ip6addr_ntoa_r(reinterpret_cast<const ip6_addr_t*>(&info.got_ip6.ip6_info.ip),
                     buf, sizeof(buf));
      _notifyStaGotIp6(buf);
    },
    ARDUINO_EVENT_WIFI_STA_GOT_IP6);

  // ---- AP ----
  _evtApStart = WiFi.onEvent(
    [this](arduino_event_id_t, arduino_event_info_t) { /* вже covered _startApInternal */ },
    ARDUINO_EVENT_WIFI_AP_START);

  _evtApStop = WiFi.onEvent(
    [this](arduino_event_id_t, arduino_event_info_t) { /* вже covered _notifyApStopped */ },
    ARDUINO_EVENT_WIFI_AP_STOP);

  _evtApStaConn = WiFi.onEvent(
    [this](arduino_event_id_t, arduino_event_info_t info) {
      _notifyApClientConnected(macToStr(info.wifi_ap_staconnected.mac));
    },
    ARDUINO_EVENT_WIFI_AP_STACONNECTED);

  _evtApStaDisconn = WiFi.onEvent(
    [this](arduino_event_id_t, arduino_event_info_t info) {
      _notifyApClientDisconnected(macToStr(info.wifi_ap_stadisconnected.mac));
    },
    ARDUINO_EVENT_WIFI_AP_STADISCONNECTED);

  _evtApIpAssigned = WiFi.onEvent(
    [this](arduino_event_id_t, arduino_event_info_t info) {
      _notifyApClientIpAssigned(
        macToStr(info.wifi_ap_staipassigned.mac),
        IPAddress(info.wifi_ap_staipassigned.ip.addr).toString().c_str());
    },
    ARDUINO_EVENT_WIFI_AP_STAIPASSIGNED);

  _evtApProbe = WiFi.onEvent(
    [this](arduino_event_id_t, arduino_event_info_t info) {
      _notifyApProbeRequestReceived(
        macToStr(info.wifi_ap_probereqrecved.mac),
        static_cast<int8_t>(info.wifi_ap_probereqrecved.rssi));
    },
    ARDUINO_EVENT_WIFI_AP_PROBEREQRECVED);

  _evtApGotIp6 = WiFi.onEvent(
    [this](arduino_event_id_t, arduino_event_info_t info) {
      char buf[40] = {};
      ip6addr_ntoa_r(reinterpret_cast<const ip6_addr_t*>(&info.got_ip6.ip6_info.ip),
                     buf, sizeof(buf));
      _notifyApGotIp6(buf);
    },
    ARDUINO_EVENT_WIFI_AP_GOT_IP6);

  // ---- WPS ----
  _evtWpsSuccess = WiFi.onEvent(
    [this](arduino_event_id_t, arduino_event_info_t) {
      if (_state != NetworkSupervisorState::WPS_WAITING) return;
      // Після ARDUINO_EVENT_WPS_ER_SUCCESS WiFi.SSID()/psk() містять дані
      _wpsSsid = WiFi.SSID().c_str();
      _wpsPassword = WiFi.psk().c_str();
      _wpsSuccess = true;
      _wpsDone = true;
      esp_wifi_wps_disable();
    },
    ARDUINO_EVENT_WPS_ER_SUCCESS);

  _evtWpsFailed = WiFi.onEvent(
    [this](arduino_event_id_t, arduino_event_info_t) {
      if (_state != NetworkSupervisorState::WPS_WAITING) return;
      esp_wifi_wps_disable();
      _wpsSuccess = false;
      _wpsDone = true;
      _notifyWpsFailed();
    },
    ARDUINO_EVENT_WPS_ER_FAILED);

  _evtWpsTimeout = WiFi.onEvent(
    [this](arduino_event_id_t, arduino_event_info_t) {
      if (_state != NetworkSupervisorState::WPS_WAITING) return;
      esp_wifi_wps_disable();
      _wpsSuccess = false;
      _wpsDone = true;
      _notifyWpsTimeout();
    },
    ARDUINO_EVENT_WPS_ER_TIMEOUT);

  _evtWpsPin = WiFi.onEvent(
    [this](arduino_event_id_t, arduino_event_info_t info) {
      if (_state != NetworkSupervisorState::WPS_WAITING) return;
      // info.wps_er_pin.pin_code — uint8_t[8], кожен байт = одна цифра
      char pin[9] = {};
      for (int i = 0; i < 8; i++) pin[i] = '0' + info.wps_er_pin.pin_code[i];
      _notifyWpsPinGenerated(pin);
    },
    ARDUINO_EVENT_WPS_ER_PIN);
}

void NetworkSupervisor::_unregisterWifiEvents() {
  WiFi.removeEvent(_evtReady);
  WiFi.removeEvent(_evtStaStart);
  WiFi.removeEvent(_evtStaStop);
  WiFi.removeEvent(_evtStaConnected);
  WiFi.removeEvent(_evtStaGotIp);
  WiFi.removeEvent(_evtStaLostIp);
  WiFi.removeEvent(_evtStaDisconnected);
  WiFi.removeEvent(_evtStaAuthMode);
  WiFi.removeEvent(_evtStaGotIp6);
  WiFi.removeEvent(_evtApStart);
  WiFi.removeEvent(_evtApStop);
  WiFi.removeEvent(_evtApStaConn);
  WiFi.removeEvent(_evtApStaDisconn);
  WiFi.removeEvent(_evtApIpAssigned);
  WiFi.removeEvent(_evtApProbe);
  WiFi.removeEvent(_evtApGotIp6);
  WiFi.removeEvent(_evtWpsSuccess);
  WiFi.removeEvent(_evtWpsFailed);
  WiFi.removeEvent(_evtWpsTimeout);
  WiFi.removeEvent(_evtWpsPin);
}
#endif  // ESP32

// ============================================================
// Notify — STA системні
// ============================================================

void NetworkSupervisor::_notifyWifiReady() {
  for (auto& e : _listeners) e.listener->onWifiReady();
}
void NetworkSupervisor::_notifyStaStart() {
  for (auto& e : _listeners) e.listener->onStaStart();
}
void NetworkSupervisor::_notifyStaStop() {
  for (auto& e : _listeners) e.listener->onStaStop();
}
void NetworkSupervisor::_notifyStaConnected(const std::string& ssid, uint8_t channel) {
  for (auto& e : _listeners) e.listener->onStaConnected(ssid, channel);
}
void NetworkSupervisor::_notifyStaAuthModeChanged(uint8_t oldMode, uint8_t newMode) {
  for (auto& e : _listeners) e.listener->onStaAuthModeChanged(oldMode, newMode);
}
void NetworkSupervisor::_notifyStaLostIp() {
  for (auto& e : _listeners) e.listener->onStaLostIp();
}
void NetworkSupervisor::_notifyStaGotIp6(const std::string& ip6) {
  for (auto& e : _listeners) e.listener->onStaGotIp6(ip6);
}

// ============================================================
// Notify — AP клієнти
// ============================================================

void NetworkSupervisor::_notifyApClientConnected(const std::string& mac) {
  for (auto& e : _listeners) e.listener->onApClientConnected(mac);
}
void NetworkSupervisor::_notifyApClientDisconnected(const std::string& mac) {
  for (auto& e : _listeners) e.listener->onApClientDisconnected(mac);
}
void NetworkSupervisor::_notifyApClientIpAssigned(const std::string& mac,
                                                   const std::string& ip) {
  for (auto& e : _listeners) e.listener->onApClientIpAssigned(mac, ip);
}
void NetworkSupervisor::_notifyApProbeRequestReceived(const std::string& mac, int8_t rssi) {
  for (auto& e : _listeners) e.listener->onApProbeRequestReceived(mac, rssi);
}
void NetworkSupervisor::_notifyApGotIp6(const std::string& ip6) {
  for (auto& e : _listeners) e.listener->onApGotIp6(ip6);
}

// ============================================================
// Notify — WPS
// ============================================================

void NetworkSupervisor::_notifyWpsStart(uint8_t method) {
  for (auto& e : _listeners) e.listener->onWpsStart(method);
}
void NetworkSupervisor::_notifyWpsSuccess(const std::string& ssid,
                                          const std::string& password) {
  for (auto& e : _listeners) e.listener->onWpsSuccess(ssid, password);
}
void NetworkSupervisor::_notifyWpsFailed() {
  for (auto& e : _listeners) e.listener->onWpsFailed();
}
void NetworkSupervisor::_notifyWpsTimeout() {
  for (auto& e : _listeners) e.listener->onWpsTimeout();
}
void NetworkSupervisor::_notifyWpsPinGenerated(const std::string& pin) {
  for (auto& e : _listeners) e.listener->onWpsPinGenerated(pin);
}
