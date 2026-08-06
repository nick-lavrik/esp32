#pragma once
#include <stdint.h>
#include <string>
#include <vector>

#include "WifiConnection.hpp"

// Інтерфейс слухача подій NetworkSupervisor.
//
// Покриває всі WiFi ARDUINO_EVENT_* з arduino-esp32 (крім WPS та ETH —
// не підтримуються NetworkSupervisor'ом) + власні події FSM supervisor'а.
//
// Всі методи мають порожню реалізацію — перевизначай тільки потрібні.
//
// Використання:
//   class MyListener : public INetworkSupervisorListener {
//    public:
//     void onConnected(const WifiConnection& conn, const std::string& ip) override {
//       Serial.printf("Connected to %s, IP: %s\n", conn.ssid.c_str(), ip.c_str());
//     }
//     void onScanResult(const std::vector<WifiConnection*>& known) override {
//       for (auto* c : known) {
//         if (c->rssi < -85) c->setEnabled(false);
//       }
//     }
//     void onApClientConnected(const std::string& mac) override {
//       Serial.printf("AP client: %s\n", mac.c_str());
//     }
//   };
//
//   NsListenerId id = ns.addListener(&myListener);
//   ns.removeListener(id);

class INetworkSupervisorListener {
 public:
  virtual ~INetworkSupervisorListener() = default;

  // ================================================================
  // FSM / Supervisor-рівень
  // ================================================================

  // Викликається перед початком WiFi-скану (FSM входить у SCANNING).
  virtual void onScanStart() {}

  // Викликається після завершення скану з переліком відомих мереж, що видно в ефірі.
  // known — вказівники на WifiConnection; rssi заповнено; можна змінити стан через
  // setEnabled() / setPriority().
  virtual void onScanResult(const std::vector<WifiConnection*>& known) {}

  // Викликається після застосування змін від onScanResult.
  // known — фінальний стан переліку після усіх маніпуляцій слухачів.
  virtual void onScanEnd(const std::vector<WifiConnection*>& known) {}

  // Викликається перед спробою підключення до мережі (FSM: CONNECTING).
  virtual void onConnecting(const WifiConnection& conn) {}

  // Викликається після успішного отримання IP (підтверджено ARDUINO_EVENT_WIFI_STA_GOT_IP).
  virtual void onConnected(const WifiConnection& conn, const std::string& ip) {}

  // Викликається при втраті з'єднання (ARDUINO_EVENT_WIFI_STA_DISCONNECTED або LOST_IP).
  virtual void onDisconnected(const std::string& ssid) {}

  // Викликається коли всі спроби підключення до конкретної мережі вичерпано.
  virtual void onConnectionFailed(const WifiConnection& conn) {}

  // Викликається після успішного запуску AP.
  virtual void onApStarted(const std::string& apSsid, const std::string& ip) {}

  // Викликається після зупинки AP.
  virtual void onApStopped() {}

  // ================================================================
  // WiFi STA — низькорівневі системні події (gate до ARDUINO_EVENT_*)
  // ================================================================

  // ARDUINO_EVENT_WIFI_READY — WiFi-драйвер ініціалізовано і готовий.
  // Викликається один раз при старті WiFi.
  virtual void onWifiReady() {}

  // ARDUINO_EVENT_WIFI_STA_START — STA-інтерфейс запущено (після WiFi.begin()).
  virtual void onStaStart() {}

  // ARDUINO_EVENT_WIFI_STA_STOP — STA-інтерфейс зупинено (після WiFi.disconnect(true)).
  virtual void onStaStop() {}

  // ARDUINO_EVENT_WIFI_STA_CONNECTED — асоціація з AP завершена, але IP ще не отримано.
  // ip порожній — чекайте onConnected() для підтвердження з IP.
  virtual void onStaConnected(const std::string& ssid, uint8_t channel) {}

  // ARDUINO_EVENT_WIFI_STA_AUTHMODE_CHANGE — тип автентифікації AP змінився.
  // oldMode / newMode: значення wifi_auth_mode_t (0=OPEN, 2=WPA2, 4=WPA3, ...).
  virtual void onStaAuthModeChanged(uint8_t oldMode, uint8_t newMode) {}

  // ARDUINO_EVENT_WIFI_STA_LOST_IP — IP-адреса скинута до 0 (DHCP lease закінчився).
  // Відрізняється від onDisconnected: фізичний зв'язок може бути ще активним.
  virtual void onStaLostIp() {}

  // ARDUINO_EVENT_WIFI_STA_GOT_IP6 — отримано IPv6-адресу на STA-інтерфейсі.
  // ESP32-only: на ESP8266 не викликається ніколи.
  virtual void onStaGotIp6(const std::string& ip6) {}

  // ================================================================
  // WiFi AP — події клієнтів точки доступу (gate до ARDUINO_EVENT_WIFI_AP_*)
  // ================================================================

  // ARDUINO_EVENT_WIFI_AP_STACONNECTED — клієнт підключився до нашого AP.
  // mac — рядок вигляду "aa:bb:cc:dd:ee:ff".
  virtual void onApClientConnected(const std::string& mac) {}

  // ARDUINO_EVENT_WIFI_AP_STADISCONNECTED — клієнт відключився від нашого AP.
  // mac — рядок вигляду "aa:bb:cc:dd:ee:ff".
  virtual void onApClientDisconnected(const std::string& mac) {}

  // ARDUINO_EVENT_WIFI_AP_STAIPASSIGNED — AP призначила IP клієнту.
  // mac + ip — ідентифікація клієнта.
  virtual void onApClientIpAssigned(const std::string& mac, const std::string& ip) {}

  // ARDUINO_EVENT_WIFI_AP_PROBEREQRECVED — отримано probe request (пристрій шукає AP).
  // Корисно для детектування присутності пристроїв навіть до підключення.
  virtual void onApProbeRequestReceived(const std::string& mac, int8_t rssi) {}

  // ARDUINO_EVENT_WIFI_AP_GOT_IP6 — отримано IPv6-адресу на AP-інтерфейсі.
  // ESP32-only: на ESP8266 не викликається ніколи.
  virtual void onApGotIp6(const std::string& ip6) {}

  // ================================================================
  // WPS — події процесу Wi-Fi Protected Setup
  // ================================================================

  // Викликається коли NetworkSupervisor починає WPS-сесію (FSM входить у WPS_WAITING).
  // method: 0 = PBC, 1 = PIN.
  virtual void onWpsStart(uint8_t method) {}

  // ARDUINO_EVENT_WPS_ER_SUCCESS (ESP32) / WPS success callback (ESP8266).
  // Викликається після успішного WPS-обміну.
  // ssid і password — дані отриманої мережі (до підключення).
  // Якщо wpsSaveOnSuccess=true — мережа вже додана до списку з'єднань.
  virtual void onWpsSuccess(const std::string& ssid, const std::string& password) {}

  // ARDUINO_EVENT_WPS_ER_FAILED (ESP32) / WPS fail callback (ESP8266).
  // Викликається при помилці WPS (невірний PIN, роутер не відповів тощо).
  virtual void onWpsFailed() {}

  // ARDUINO_EVENT_WPS_ER_TIMEOUT (ESP32).
  // Викликається коли wpsTimeoutMs вичерпано без відповіді роутера.
  // ESP8266: не розрізняє fail і timeout — onWpsFailed() покриває обидва випадки.
  virtual void onWpsTimeout() {}

  // ARDUINO_EVENT_WPS_ER_PIN (ESP32, тільки WpsMethod::PIN коли wpsPin порожній).
  // Викликається коли пристрій згенерував PIN-код для введення на роутері.
  // pin — рядок з 8 цифр, який потрібно ввести в веб-інтерфейс роутера.
  virtual void onWpsPinGenerated(const std::string& pin) {}
};
