#include "NetworkManager.hpp"

#include <ArduinoJson.h>

#include "ConfigStorage.hpp"

static constexpr char kConfigKey[] = "nm_config";
static constexpr char kConnectionsKey[] = "nm_conn";
static constexpr char kIdCounterKey[] = "nm_id_ctr";

// ============================================================
// Ctor / Dtor
// ============================================================

NetworkManager::NetworkManager(ConfigStorage* storage) : _storage(storage) {}

NetworkManager::~NetworkManager() { end(); }

// ============================================================
// Ініціалізація
// ============================================================

void NetworkManager::begin() {
  if (_state != NetworkManagerState::IDLE) return;
  _autoReconnect = _config.autoReconnect;

#if defined(NM_BLOCKING_MODE)
  _setState(NetworkManagerState::SCANNING);
#else
  _setState(NetworkManagerState::SCANNING);
  xTaskCreate(_taskEntry, "NetworkManager", 4096, this, 1, &_taskHandle);
#endif
}

void NetworkManager::end() {
#if !defined(NM_BLOCKING_MODE)
  if (_taskHandle) {
    vTaskDelete(_taskHandle);
    _taskHandle = nullptr;
  }
#endif
  if (_state == NetworkManagerState::AP_MODE) {
    WiFi.softAPdisconnect(true);
    _notifyApStopped();
  }
  WiFi.disconnect(true);
  _setState(NetworkManagerState::IDLE);
}

// ============================================================
// З'єднання
// ============================================================

uint16_t NetworkManager::addConnection(const WifiConnection& conn) {
  WifiConnection c = conn;
  c.connectionId = _nextConnectionId++;
  _connections.push_back(c);
  return c.connectionId;
}

bool NetworkManager::removeConnection(uint16_t connectionId) {
  for (auto it = _connections.begin(); it != _connections.end(); ++it) {
    if (it->connectionId == connectionId) {
      _connections.erase(it);
      return true;
    }
  }
  return false;
}

WifiConnection* NetworkManager::getConnection(uint16_t connectionId) {
  for (auto& c : _connections) {
    if (c.connectionId == connectionId) return &c;
  }
  return nullptr;
}

const std::vector<WifiConnection>& NetworkManager::connections() const { return _connections; }

// ============================================================
// Стан
// ============================================================

NetworkManagerState NetworkManager::state() const { return _state; }

bool NetworkManager::isConnected() const { return _state == NetworkManagerState::CONNECTED; }

std::string NetworkManager::currentSsid() const { return _currentSsid; }

std::string NetworkManager::localIp() const { return _currentIp; }

// ============================================================
// Управління
// ============================================================

void NetworkManager::setAutoReconnect(bool enabled) {
  _autoReconnect = enabled;
  _config.autoReconnect = enabled;
}

bool NetworkManager::autoReconnect() const { return _autoReconnect; }

void NetworkManager::reconnect() {
  WiFi.disconnect(true);
  _candidateIndex = 0;
  _currentRetries = 0;
  _setState(NetworkManagerState::SCANNING);
}

void NetworkManager::startAp() {
  if (_state == NetworkManagerState::CONNECTED) {
    WiFi.disconnect(true);
    _notifyDisconnected(_currentSsid);
    _currentSsid.clear();
    _currentIp.clear();
  }
  _startApInternal();
}

void NetworkManager::stopAp() {
  if (_state != NetworkManagerState::AP_MODE) return;
  WiFi.softAPdisconnect(true);
  _notifyApStopped();
  if (_autoReconnect) {
    _candidateIndex = 0;
    _currentRetries = 0;
    _setState(NetworkManagerState::SCANNING);
  } else {
    _setState(NetworkManagerState::IDLE);
  }
}

void NetworkManager::scan() {
#if defined(ESP8266)
  WiFi.scanNetworks();  // синхронний
#else
  WiFi.scanNetworks(true);  // асинхронний
#endif
}

// ============================================================
// Конфігурація
// ============================================================

void NetworkManager::setConfig(const NetworkManagerConfig& cfg) {
  _config = cfg;
  _autoReconnect = cfg.autoReconnect;
}

const NetworkManagerConfig& NetworkManager::config() const { return _config; }

void NetworkManager::saveConfig() {
  if (!_storage) return;

  // --- config ---
  {
    JsonDocument doc;
    doc["scanIntervalMs"] = _config.scanIntervalMs;
    doc["connectTimeoutMs"] = _config.connectTimeoutMs;
    doc["retryDelayMs"] = _config.retryDelayMs;
    doc["maxRetries"] = _config.maxRetries;
    doc["autoReconnect"] = _config.autoReconnect;
    doc["scanBeforeConnect"] = _config.scanBeforeConnect;
    doc["apSsid"] = _config.apSsid.c_str();
    doc["apPassword"] = _config.apPassword.c_str();
    doc["apChannel"] = _config.apChannel;
    doc["apIp"] = _config.apIp.c_str();

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

void NetworkManager::loadConfig() {
  if (!_storage) return;

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
        if (doc["apSsid"].is<const char*>()) _config.apSsid = doc["apSsid"].as<const char*>();
        if (doc["apPassword"].is<const char*>())
          _config.apPassword = doc["apPassword"].as<const char*>();
        _config.apChannel = doc["apChannel"] | _config.apChannel;
        if (doc["apIp"].is<const char*>()) _config.apIp = doc["apIp"].as<const char*>();
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

NmListenerId NetworkManager::addListener(INetworkManagerListener* listener) {
  if (!listener) return kInvalidNmListenerId;
  NmListenerId id = _nextListenerId++;
  _listeners.push_back({id, listener});
  return id;
}

void NetworkManager::removeListener(NmListenerId id) {
  _listeners.erase(std::remove_if(_listeners.begin(), _listeners.end(),
                                  [id](const ListenerEntry& e) { return e.id == id; }),
                   _listeners.end());
}

// ============================================================
// FSM — внутрішня логіка
// ============================================================

void NetworkManager::_setState(NetworkManagerState next) { _state = next; }

std::vector<WifiConnection*> NetworkManager::_sortedCandidates() {
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

void NetworkManager::_applyScanResults() {
  int16_t n = WiFi.scanComplete();
  if (n <= 0) return;

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

bool NetworkManager::_connectTo(WifiConnection& conn) {
  _applyIpConfig(conn);

  WiFi.begin(conn.ssid.c_str(), conn.password.c_str());

  uint32_t deadline = millis() + _config.connectTimeoutMs;
  while (WiFi.status() != WL_CONNECTED && millis() < deadline) {
#if defined(NM_BLOCKING_MODE)
    delay(100);
#else
    vTaskDelay(pdMS_TO_TICKS(100));
#endif
  }

  if (WiFi.status() == WL_CONNECTED) {
    conn.lastConnected = static_cast<uint32_t>(millis() / 1000);  // грубий timestamp
    _currentSsid = conn.ssid;
    _currentIp = WiFi.localIP().toString().c_str();
    return true;
  }

  WiFi.disconnect(true);
  return false;
}

void NetworkManager::_applyIpConfig(const WifiConnection& conn) {
  if (conn.staticIp && !conn.ip.empty()) {
    IPAddress ip, gateway, subnet, dns;
    ip.fromString(conn.ip.c_str());
    gateway.fromString(conn.gateway.c_str());
    subnet.fromString(conn.subnet.c_str());
    dns.fromString(conn.dns.empty() ? "8.8.8.8" : conn.dns.c_str());
    WiFi.config(ip, gateway, subnet, dns);
  } else {
#if defined(ESP32)
    // скидаємо статику на DHCP
    WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE);
#endif
  }
}

void NetworkManager::_startApInternal() {
  _setState(NetworkManagerState::STARTING_AP);

  WiFi.mode(WIFI_AP);
  IPAddress apIp;
  apIp.fromString(_config.apIp.c_str());
  WiFi.softAPConfig(apIp, apIp, IPAddress(255, 255, 255, 0));

  if (_config.apPassword.empty()) {
    WiFi.softAP(_config.apSsid.c_str(), nullptr, _config.apChannel);
  } else {
    WiFi.softAP(_config.apSsid.c_str(), _config.apPassword.c_str(), _config.apChannel);
  }

  _setState(NetworkManagerState::AP_MODE);
  _lastScanMs = millis();
  _notifyApStarted(_config.apSsid, _config.apIp);
}

// ============================================================
// FSM — основний цикл
// ============================================================

void NetworkManager::_taskLoop() {
  while (true) {
    switch (_state) {
      // ----------------------------------------------------------
      case NetworkManagerState::SCANNING: {
        _notifyScanStart();

        if (_config.scanBeforeConnect) {
#if defined(ESP8266)
          WiFi.scanNetworks();  // синхронний, блокуючий ~2-3 сек
          _applyScanResults();
#else
          WiFi.scanNetworks(true);  // асинхронний
          // чекаємо завершення
          while (WiFi.scanComplete() == WIFI_SCAN_RUNNING) {
            vTaskDelay(pdMS_TO_TICKS(200));
          }
          _applyScanResults();
#endif
        }

        auto candidates = _sortedCandidates();

        if (_config.scanBeforeConnect) {
          // залишаємо тільки видимі (rssi != 0)
          candidates.erase(
              std::remove_if(candidates.begin(), candidates.end(),
                             [](const WifiConnection* c) { return c->rssi == 0; }),
              candidates.end());
        }

        _notifyScanResult(candidates);
        _notifyScanEnd(candidates);

        _candidateIndex = 0;
        _currentRetries = 0;

        if (candidates.empty()) {
          _startApInternal();
          break;
        }

        _setState(NetworkManagerState::CONNECTING);
        break;
      }

      // ----------------------------------------------------------
      case NetworkManagerState::CONNECTING: {
        auto candidates = _sortedCandidates();
        if (_config.scanBeforeConnect) {
          candidates.erase(
              std::remove_if(candidates.begin(), candidates.end(),
                             [](const WifiConnection* c) { return c->rssi == 0; }),
              candidates.end());
        }

        if (_candidateIndex >= candidates.size()) {
          // всі кандидати вичерпано
          _startApInternal();
          break;
        }

        WifiConnection* conn = candidates[_candidateIndex];
        _notifyConnecting(*conn);

        if (_connectTo(*conn)) {
          _setState(NetworkManagerState::CONNECTED);
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
#if defined(NM_BLOCKING_MODE)
          delay(_config.retryDelayMs);
#else
          vTaskDelay(pdMS_TO_TICKS(_config.retryDelayMs));
#endif
        }
        break;
      }

      // ----------------------------------------------------------
      case NetworkManagerState::CONNECTED: {
        if (WiFi.status() != WL_CONNECTED) {
          std::string lost = _currentSsid;
          _currentSsid.clear();
          _currentIp.clear();
          _notifyDisconnected(lost);

          if (_autoReconnect) {
            _candidateIndex = 0;
            _currentRetries = 0;
            _setState(NetworkManagerState::RECONNECTING);
          }
        }
#if defined(NM_BLOCKING_MODE)
        delay(500);
#else
        vTaskDelay(pdMS_TO_TICKS(500));
#endif
        break;
      }

      // ----------------------------------------------------------
      case NetworkManagerState::RECONNECTING: {
        // переходимо одразу на SCANNING для повного перебору
        _setState(NetworkManagerState::SCANNING);
        break;
      }

      // ----------------------------------------------------------
      case NetworkManagerState::AP_MODE: {
        if (!_autoReconnect) {
#if defined(NM_BLOCKING_MODE)
          delay(1000);
#else
          vTaskDelay(pdMS_TO_TICKS(1000));
#endif
          break;
        }
        // перевіряємо інтервал сканування
        if (millis() - _lastScanMs >= _config.scanIntervalMs) {
          WiFi.softAPdisconnect(true);
          _notifyApStopped();
          _candidateIndex = 0;
          _currentRetries = 0;
          _setState(NetworkManagerState::SCANNING);
        } else {
#if defined(NM_BLOCKING_MODE)
          delay(1000);
#else
          vTaskDelay(pdMS_TO_TICKS(1000));
#endif
        }
        break;
      }

      // ----------------------------------------------------------
      default:
#if defined(NM_BLOCKING_MODE)
        delay(100);
#else
        vTaskDelay(pdMS_TO_TICKS(100));
#endif
        break;
    }
  }
}

// ============================================================
// FreeRTOS task entry / blocking loop
// ============================================================

#if defined(NM_BLOCKING_MODE)
void NetworkManager::loop() { _taskLoop(); }
#else
void NetworkManager::_taskEntry(void* param) {
  static_cast<NetworkManager*>(param)->_taskLoop();
  vTaskDelete(nullptr);
}
#endif

// ============================================================
// Notify helpers
// ============================================================

void NetworkManager::_notifyScanStart() {
  for (auto& e : _listeners) e.listener->onScanStart();
}

void NetworkManager::_notifyScanResult(const std::vector<WifiConnection*>& known) {
  for (auto& e : _listeners) e.listener->onScanResult(known);
}

void NetworkManager::_notifyScanEnd(const std::vector<WifiConnection*>& known) {
  for (auto& e : _listeners) e.listener->onScanEnd(known);
}

void NetworkManager::_notifyConnecting(const WifiConnection& conn) {
  for (auto& e : _listeners) e.listener->onConnecting(conn);
}

void NetworkManager::_notifyConnected(const WifiConnection& conn, const std::string& ip) {
  for (auto& e : _listeners) e.listener->onConnected(conn, ip);
}

void NetworkManager::_notifyDisconnected(const std::string& ssid) {
  for (auto& e : _listeners) e.listener->onDisconnected(ssid);
}

void NetworkManager::_notifyConnectionFailed(const WifiConnection& conn) {
  for (auto& e : _listeners) e.listener->onConnectionFailed(conn);
}

void NetworkManager::_notifyApStarted(const std::string& apSsid, const std::string& ip) {
  for (auto& e : _listeners) e.listener->onApStarted(apSsid, ip);
}

void NetworkManager::_notifyApStopped() {
  for (auto& e : _listeners) e.listener->onApStopped();
}
