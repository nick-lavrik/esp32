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

// WiFi connection manager з підтримкою списку з'єднань, пріоритетів,
// автоперепідключення та AP fallback. Натхнений концепцією nmcli (Ubuntu).
//
// --- Швидкий старт ---
//
//   NetworkManager nm;
//
//   WifiConnection home;
//   home.ssid     = "HomeNet";
//   home.password = "secret";
//   home.priority = 10;
//   nm.addConnection(home);
//
//   WifiConnection office;
//   office.ssid     = "OfficeNet";
//   office.password = "work123";
//   office.priority = 5;
//   nm.addConnection(office);
//
//   NetworkManagerConfig cfg;
//   cfg.apSsid    = "ESP32-Fallback";
//   cfg.apPassword = "12345678";
//   nm.setConfig(cfg);
//
//   nm.begin();   // запускає FreeRTOS-таск (або блокує до connect/AP у BLOCKING_MODE)
//
// --- З ConfigStorage ---
//
//   ConfigStorage storage;
//   storage.begin("netmgr");
//   nm.setStorage(&storage);
//   nm.loadConfig();       // завантажує конфіг і список з'єднань
//   nm.begin();
//
// --- З listener ---
//
//   class MyListener : public INetworkManagerListener {
//    public:
//     void onConnected(const WifiConnection& c, const std::string& ip) override {
//       Serial.printf("Connected: %s  IP: %s\n", c.ssid.c_str(), ip.c_str());
//     }
//   };
//   MyListener listener;
//   ListenerId id = nm.addListener(&listener);
//   // nm.removeListener(id);

// Opaque ідентифікатор listener'а (аналогічно ListenerId в EventDispatcher).
using NmListenerId = uint16_t;
constexpr NmListenerId kInvalidNmListenerId = 0;

// Опціональна залежність — forward declaration, щоб уникнути
// обов'язкового включення ConfigStorage.hpp у всіх translation units.
class ConfigStorage;

class NetworkManager {
 public:
  NetworkManager();
  ~NetworkManager();

  // Некопійований: ListenerId і FreeRTOS task handle прив'язані до екземпляра.
  NetworkManager(const NetworkManager&) = delete;
  NetworkManager& operator=(const NetworkManager&) = delete;

  // ---- ініціалізація ----

  // Запускає FSM (FreeRTOS task на ESP32/ESP8266, або входить у blocking loop).
  // Якщо storage встановлений — конфіг і список з'єднань вже мають бути завантажені
  // через loadConfig() до виклику begin().
  void begin();

  // Зупиняє FSM, від'єднується від WiFi, зупиняє AP якщо запущений.
  void end();

  // ---- ConfigStorage (опціонально) ----

  void setStorage(ConfigStorage* storage);

  // Завантажує NetworkManagerConfig і список WifiConnection з ConfigStorage.
  // Повертає false якщо storage не встановлений або дані відсутні.
  bool loadConfig();

  // Зберігає поточний конфіг і список з'єднань у ConfigStorage.
  // Повертає false якщо storage не встановлений.
  bool saveConfig();

  // ---- конфігурація ----

  void setConfig(const NetworkManagerConfig& config);
  const NetworkManagerConfig& config() const;

  // ---- з'єднання ----

  // Додає з'єднання до списку. Призначає унікальний connectionId.
  // Повертає призначений connectionId (або 0 при помилці).
  uint16_t addConnection(const WifiConnection& conn);

  // Видаляє з'єднання за connectionId. Повертає false якщо не знайдено.
  bool removeConnection(uint16_t connectionId);

  // Повертає вказівник на з'єднання або nullptr якщо не знайдено.
  WifiConnection* getConnection(uint16_t connectionId);
  const WifiConnection* getConnection(uint16_t connectionId) const;

  // Повний список збережених з'єднань (для ітерації / веб-інтерфейсу).
  const std::vector<WifiConnection>& connections() const;

  // ---- стан ----

  NetworkManagerState state() const;
  bool isConnected() const;
  std::string currentSsid() const;
  std::string localIp() const;

  // ---- управління ----

  // Вмикає/вимикає автоматичне перепідключення та скан у AP_MODE.
  // false → менеджер залишається у поточному стані до явного виклику.
  void setAutoReconnect(bool enabled);
  bool autoReconnect() const;

  // Форсує новий цикл: scan → connect → (AP якщо всі невдалі).
  // Ігнорує autoReconnect.
  void reconnect();

  // Форсовано запускає AP mode (незалежно від стану підключення).
  void startAp();

  // Зупиняє AP mode. Якщо autoReconnect=true — запускає scan.
  void stopAp();

  // Форсований WiFi scan (оновлює rssi у відомих з'єднань, викликає onScan* події).
  void scan();

  // ---- listeners ----

  NmListenerId addListener(INetworkManagerListener* listener);
  void removeListener(NmListenerId id);

 private:
  // ---- FSM ----

  void _setState(NetworkManagerState newState);
  void _runFsm();  // один крок FSM — викликається з task або loop()

  // ---- FSM steps ----

  void _doScan();
  void _doConnect();         // намагається підключитись до наступної мережі зі списку
  void _doStartAp();
  void _doReconnect();
  void _checkConnected();    // перевірка втрати з'єднання у стані CONNECTED

  // ---- helpers ----

  // Будує впорядкований список кандидатів для підключення:
  //   1. isEnabled=true
  //   2. якщо scanBeforeConnect — тільки ті що видно (rssi != 0)
  //   3. сортування: lastConnected DESC (спочатку), потім priority DESC
  std::vector<WifiConnection*> _buildCandidateList();

  // Спроба підключення до конкретного з'єднання.
  // Повертає true якщо отримано IP у межах connectTimeoutMs.
  bool _connectTo(WifiConnection& conn);

  // Оновлює rssi у _connections на основі результатів WiFi.scanNetworks().
  void _applyScanResults();

  // Диспетчер подій до listeners.
  void _notifyScanStart();
  void _notifyScanResult();
  void _notifyScanEnd();
  void _notifyConnecting(const WifiConnection& conn);
  void _notifyConnected(const WifiConnection& conn, const std::string& ip);
  void _notifyDisconnected(const std::string& ssid);
  void _notifyConnectionFailed(const WifiConnection& conn);
  void _notifyApStarted(const std::string& apSsid, const std::string& ip);
  void _notifyApStopped();

  uint16_t _nextConnectionId();

  // ---- FreeRTOS task ----

#if !defined(NM_BLOCKING_MODE)
  static void _taskEntry(void* arg);
  void* _taskHandle = nullptr;  // TaskHandle_t — void* щоб не тягнути freertos у header
#endif

  // ---- стан ----

  NetworkManagerState _state = NetworkManagerState::IDLE;
  NetworkManagerConfig _config;
  std::vector<WifiConnection> _connections;
  uint16_t _nextId = 1;

  bool _autoReconnect = true;

  // поточне активне з'єднання (заповнюється після CONNECTED)
  std::string _currentSsid;
  std::string _currentIp;

  // індекс у _buildCandidateList() — яку мережу пробуємо зараз
  size_t _candidateIndex = 0;
  uint8_t _retryCount = 0;

  // мітка часу для scanInterval в AP_MODE / RECONNECTING
  uint32_t _lastScanMs = 0;

  // форсовані команди з публічного API (встановлюються атомарно)
  volatile bool _cmdReconnect = false;
  volatile bool _cmdStartAp = false;
  volatile bool _cmdStopAp = false;
  volatile bool _cmdScan = false;

  // ---- storage ----
  ConfigStorage* _storage = nullptr;

  // ---- listeners ----

  struct ListenerEntry {
    NmListenerId id;
    INetworkManagerListener* listener;
  };
  std::vector<ListenerEntry> _listeners;
  NmListenerId _nextListenerId = 1;
};
