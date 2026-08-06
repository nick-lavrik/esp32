#pragma once

#include <stdint.h>
#include <string>
#include <vector>

#if defined(ESP8266)
#include <ESP8266WiFi.h>
#else
#include <WiFi.h>
#include <esp_wps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#endif

#include "INetworkSupervisorListener.hpp"
#include "NetworkSupervisorConfig.hpp"
#include "NetworkSupervisorState.hpp"
#include "WifiConnection.hpp"

class ConfigStorage;

// opaque handle для реєстрації/видалення listener'ів
using NsListenerId = uint16_t;
static constexpr NsListenerId kInvalidNsListenerId = 0;

// WiFi connection manager з пріоритетним списком мереж і AP-fallback.
//
// Швидкий старт (ESP32, FreeRTOS task):
//   ConfigStorage storage;
//   storage.begin("netmgr");
//
//   NetworkSupervisorConfig cfg;
//   cfg.apSsid = "ESP32-Setup";
//
//   NetworkSupervisor nm(&storage);
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

class NetworkSupervisor {
 public:
  explicit NetworkSupervisor(ConfigStorage* storage = nullptr);
  ~NetworkSupervisor();

  // Некопійований — керує FreeRTOS task і внутрішнім станом
  NetworkSupervisor(const NetworkSupervisor&) = delete;
  NetworkSupervisor& operator=(const NetworkSupervisor&) = delete;

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

  NetworkSupervisorState state() const;
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

  // Ручний запуск WPS (PBC або PIN — з поточного config.wpsMethod).
  // Можна викликати з будь-якого стану; перериває поточне з'єднання/AP.
  // При WpsMethod::PIN і порожньому config.wpsPin — пристрій генерує PIN
  // і викликає onWpsPinGenerated() у listeners.
  void startWps();

  // ---- конфігурація ----

  void setConfig(const NetworkSupervisorConfig& cfg);
  const NetworkSupervisorConfig& config() const;

  // Зберігає config + список з'єднань в ConfigStorage ("nm_config", "nm_connections").
  void saveConfig();

  // Завантажує config + список з'єднань з ConfigStorage.
  void loadConfig();

  // ---- listeners ----

  NsListenerId addListener(INetworkSupervisorListener* listener);
  void removeListener(NsListenerId id);

 private:
  // ---- FSM ----

  void _setState(NetworkSupervisorState next);

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
  // ---- WPS ----
  void _startWpsInternal();
  bool _pollWpsResult();  // повертає true коли результат відомий (success або fail)
  void _onWpsSuccess(const std::string& ssid, const std::string& password);

  // FSM-рівень
  void _notifyScanStart();
  void _notifyScanResult(const std::vector<WifiConnection*>& known);
  void _notifyScanEnd(const std::vector<WifiConnection*>& known);
  void _notifyConnecting(const WifiConnection& conn);
  void _notifyConnected(const WifiConnection& conn, const std::string& ip);
  void _notifyDisconnected(const std::string& ssid);
  void _notifyConnectionFailed(const WifiConnection& conn);
  void _notifyApStarted(const std::string& apSsid, const std::string& ip);
  void _notifyApStopped();

  // Gate до ARDUINO_EVENT_WIFI_STA_*
  void _notifyWifiReady();
  void _notifyStaStart();
  void _notifyStaStop();
  void _notifyStaConnected(const std::string& ssid, uint8_t channel);
  void _notifyStaAuthModeChanged(uint8_t oldMode, uint8_t newMode);
  void _notifyStaLostIp();
  void _notifyStaGotIp6(const std::string& ip6);

  // Gate до ARDUINO_EVENT_WPS_ER_*
  void _notifyWpsStart(uint8_t method);
  void _notifyWpsSuccess(const std::string& ssid, const std::string& password);
  void _notifyWpsFailed();
  void _notifyWpsTimeout();
  void _notifyWpsPinGenerated(const std::string& pin);

  // Gate до ARDUINO_EVENT_WIFI_AP_*
  void _notifyApClientConnected(const std::string& mac);
  void _notifyApClientDisconnected(const std::string& mac);
  void _notifyApClientIpAssigned(const std::string& mac, const std::string& ip);
  void _notifyApProbeRequestReceived(const std::string& mac, int8_t rssi);
  void _notifyApGotIp6(const std::string& ip6);

  // Реєстрація/видалення системного WiFi event handler (викликається з begin()/end())
  void _registerWifiEvents();
  void _unregisterWifiEvents();

  // Ідентифікатори зареєстрованих WiFi.onEvent() хендлерів (для removeEvent)
#if defined(ESP8266)
  WiFiEventHandler _wifiApConnHandler;
  WiFiEventHandler _wifiApDisconnHandler;
#else
  WiFiEventId_t _evtReady = 0;
  WiFiEventId_t _evtStaStart = 0;
  WiFiEventId_t _evtStaStop = 0;
  WiFiEventId_t _evtStaConnected = 0;
  WiFiEventId_t _evtStaGotIp = 0;
  WiFiEventId_t _evtStaLostIp = 0;
  WiFiEventId_t _evtStaDisconnected = 0;
  WiFiEventId_t _evtStaAuthMode = 0;
  WiFiEventId_t _evtStaGotIp6 = 0;
  WiFiEventId_t _evtApStart = 0;
  WiFiEventId_t _evtApStop = 0;
  WiFiEventId_t _evtApStaConn = 0;
  WiFiEventId_t _evtApStaDisconn = 0;
  WiFiEventId_t _evtApIpAssigned = 0;
  WiFiEventId_t _evtApProbe = 0;
  WiFiEventId_t _evtApGotIp6 = 0;
  WiFiEventId_t _evtWpsSuccess = 0;
  WiFiEventId_t _evtWpsFailed = 0;
  WiFiEventId_t _evtWpsTimeout = 0;
  WiFiEventId_t _evtWpsPin = 0;
#endif

  // ---- FreeRTOS task ----

#if defined(NM_BLOCKING_MODE)
  // Варіант C: без task, loop() викликається ззовні
 public:
  void loop();

 private:
#elif !defined(ESP8266)
  // Варіант A: ESP32 FreeRTOS task
  static void _taskEntry(void* param);
#endif
  // _taskLoop() — спільний для всіх варіантів (ESP32 task / ESP8266 loop / blocking)
  void _taskLoop();

  // ---- дані ----

  ConfigStorage* _storage;
  NetworkSupervisorConfig _config;
  NetworkSupervisorState _state = NetworkSupervisorState::IDLE;

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

  // WPS runtime
  uint32_t _wpsStartMs = 0;    // millis() на момент початку WPS
  bool _wpsSuccess = false;    // результат встановлюється з WiFi event (ESP32)
  bool _wpsDone = false;       // WPS завершено (success або fail/timeout)
  std::string _wpsSsid;        // SSID отриманий після успішного WPS
  std::string _wpsPassword;    // пароль отриманий після успішного WPS

  // Listeners
  struct ListenerEntry {
    NsListenerId id;
    INetworkSupervisorListener* listener;
  };
  std::vector<ListenerEntry> _listeners;
  NsListenerId _nextListenerId = 1;

#if !defined(NM_BLOCKING_MODE) && !defined(ESP8266)
  TaskHandle_t _taskHandle = nullptr;
#endif
};
