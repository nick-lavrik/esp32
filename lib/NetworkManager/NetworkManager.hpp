#pragma once

#include <stdint.h>
#include <string>
#include <vector>

#if defined(ESP8266)
#include <ESP8266WiFi.h>
#else
#include <WiFi.h>
#endif

#include "INetworkManagerListener.hpp"
#include "NetworkManagerConfig.hpp"
#include "NetworkManagerState.hpp"
#include "WifiConnection.hpp"

class ConfigStorage;

// opaque handle для реєстрації/видалення listener'ів
using NmListenerId = uint16_t;
static constexpr NmListenerId kInvalidNmListenerId = 0;

// WiFi connection manager з пріоритетним списком мереж і AP-fallback.
//
// Швидкий старт (ESP32, FreeRTOS task):
//   ConfigStorage storage;
//   storage.begin("netmgr");
//
//   NetworkManagerConfig cfg;
//   cfg.apSsid = "ESP32-Setup";
//
//   NetworkManager nm(&storage);
//   nm.setConfig(cfg);
//   nm.loadConfig();                     // завантажити збережені з'єднання
//
//   WifiConnection home;
//   home.ssid = "HomeWiFi";
//   home.password = "secret";
//   home.priority = 10;
//   nm.addConnection(home);
//
//   nm.addListener(&myListener);
//   nm.begin();                          // стартує FreeRTOS task
//
// Після підключення:
//   Serial.println(nm.localIp().c_str());
//
// Ручне управління:
//   nm.setAutoReconnect(false);          // зупинити автоперепідключення
//   nm.scan();                           // форсований скан
//   nm.startAp();                        // форсований AP mode
//   nm.saveConfig();                     // зберегти поточну конфігурацію

class NetworkManager {
 public:
  explicit NetworkManager(ConfigStorage* storage = nullptr);
  ~NetworkManager();

  // Некопійований — керує FreeRTOS task і внутрішнім станом
  NetworkManager(const NetworkManager&) = delete;
  NetworkManager& operator=(const NetworkManager&) = delete;

  // ---- ініціалізація ----

  // Запускає FSM (FreeRTOS task на ESP32/ESP8266, або blocking на NM_BLOCKING_MODE).
  // Викликати після setConfig() / loadConfig() / addConnection().
  void begin();

  // Зупиняє task, відключається від WiFi, зупиняє AP.
  void end();

  // ---- з'єднання ----

  // Додає з'єднання до списку. Генерує і повертає connectionId.
  // Не зберігає автоматично — викличте saveConfig() за потреби.
  uint16_t addConnection(const WifiConnection& conn);

  // Видаляє з'єднання за connectionId. Повертає false якщо не знайдено.
  bool removeConnection(uint16_t connectionId);

  // Повертає вказівник на з'єднання або nullptr якщо не знайдено.
  WifiConnection* getConnection(uint16_t connectionId);

  // Повний список збережених з'єднань (тільки читання).
  const std::vector<WifiConnection>& connections() const;

  // ---- стан ----

  NetworkManagerState state() const;
  bool isConnected() const;
  std::string currentSsid() const;
  std::string localIp() const;

  // ---- управління ----

  // Вмикає/вимикає автоматичне перепідключення і повторне сканування.
  // false → менеджер залишається в поточному стані до ручного виклику.
  void setAutoReconnect(bool enabled);
  bool autoReconnect() const;

  // Форсований reconnect: перериває поточний стан і починає SCANNING.
  void reconnect();

  // Форсований перехід в AP_MODE (наприклад, з SerialCommander).
  void startAp();

  // Зупиняє AP і переходить в SCANNING (якщо autoReconnect=true).
  void stopAp();

  // Форсований WiFi-скан без зміни поточного з'єднання.
  void scan();

  // ---- конфігурація ----

  void setConfig(const NetworkManagerConfig& cfg);
  const NetworkManagerConfig& config() const;

  // Зберігає config + список з'єднань в ConfigStorage ("nm_config", "nm_connections").
  void saveConfig();

  // Завантажує config + список з'єднань з ConfigStorage.
  void loadConfig();

  // ---- listeners ----

  NmListenerId addListener(INetworkManagerListener* listener);
  void removeListener(NmListenerId id);

 private:
  // ---- FSM ----

  void _setState(NetworkManagerState next);

  // Повертає відсортований список enabled-з'єднань для спроби підключення.
  // Порядок: спочатку найновіший lastConnected, потім за priority DESC.
  std::vector<WifiConnection*> _sortedCandidates();

  // Фільтрує _sortedCandidates() залишаючи тільки ті SSID, що видно в ефірі.
  // Заповнює rssi у відповідних WifiConnection.
  void _applyScanResults();

  // Спроба підключення до одного з'єднання. Повертає true при успіху.
  bool _connectTo(WifiConnection& conn);

  // Конфігурує статичну IP або DHCP перед WiFi.begin().
  void _applyIpConfig(const WifiConnection& conn);

  // Запускає AP з параметрами з _config.
  void _startApInternal();

  // Диспетчеризація подій до всіх listeners.
  void _notifyScanStart();
  void _notifyScanResult(const std::vector<WifiConnection*>& known);
  void _notifyScanEnd(const std::vector<WifiConnection*>& known);
  void _notifyConnecting(const WifiConnection& conn);
  void _notifyConnected(const WifiConnection& conn, const std::string& ip);
  void _notifyDisconnected(const std::string& ssid);
  void _notifyConnectionFailed(const WifiConnection& conn);
  void _notifyApStarted(const std::string& apSsid, const std::string& ip);
  void _notifyApStopped();

  // ---- FreeRTOS task ----

#if defined(NM_BLOCKING_MODE)
  // Варіант C: без task, loop() викликається ззовні
 public:
  void loop();

 private:
#else
  static void _taskEntry(void* param);
  void _taskLoop();
#endif

  // ---- дані ----

  ConfigStorage* _storage;
  NetworkManagerConfig _config;
  NetworkManagerState _state = NetworkManagerState::IDLE;

  std::vector<WifiConnection> _connections;
  uint16_t _nextConnectionId = 1;

  std::string _currentSsid;
  std::string _currentIp;

  bool _autoReconnect = true;

  // Індекс поточного кандидата під час CONNECTING (індекс у _sortedCandidates()).
  // Скидається при кожному новому циклі сканування.
  size_t _candidateIndex = 0;
  uint8_t _currentRetries = 0;

  // Момент останнього скану (millis()) — для autoReconnect інтервалу в AP_MODE.
  uint32_t _lastScanMs = 0;

  // Listeners
  struct ListenerEntry {
    NmListenerId id;
    INetworkManagerListener* listener;
  };
  std::vector<ListenerEntry> _listeners;
  NmListenerId _nextListenerId = 1;

#if !defined(NM_BLOCKING_MODE)
  TaskHandle_t _taskHandle = nullptr;
#endif
};
