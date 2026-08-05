#pragma once
#include <string>
#include <vector>

#include "WifiConnection.hpp"

// Інтерфейс слухача подій NetworkManager.
//
// Всі методи мають порожню реалізацію за замовчуванням — перевизначай тільки потрібні.
//
// Використання:
//   class MyListener : public INetworkManagerListener {
//    public:
//     void onConnected(const WifiConnection& conn, const std::string& ip) override {
//       Serial.printf("Connected to %s, IP: %s\n", conn.ssid.c_str(), ip.c_str());
//     }
//     // onScanResult — можна змінити стан WifiConnection через публічні методи:
//     void onScanResult(const std::vector<WifiConnection*>& known) override {
//       for (auto* c : known) {
//         if (c->rssi < -85) c->setEnabled(false);
//       }
//     }
//   };
//
//   ListenerId id = nm.addListener(&myListener);
//   nm.removeListener(id);

class INetworkManagerListener {
 public:
  virtual ~INetworkManagerListener() = default;

  // ---- сканування ----

  // Викликається перед початком WiFi-скану.
  virtual void onScanStart() {}

  // Викликається після завершення скану з переліком відомих мереж, що видно в ефірі.
  // known — вказівники на WifiConnection зі списку менеджера (не const об'єкти —
  // можна змінити стан через setEnabled() / setPriority()).
  // rssi заповнено актуальним значенням для кожної видимої мережі.
  virtual void onScanResult(const std::vector<WifiConnection*>& known) {}

  // Викликається після завершення скану та застосування змін від onScanResult.
  // known — фінальний стан переліку відомих мереж після усіх маніпуляцій слухачів.
  virtual void onScanEnd(const std::vector<WifiConnection*>& known) {}

  // ---- підключення ----

  // Викликається перед спробою підключення до мережі.
  virtual void onConnecting(const WifiConnection& conn) {}

  // Викликається після успішного отримання IP.
  virtual void onConnected(const WifiConnection& conn, const std::string& ip) {}

  // Викликається при втраті з'єднання (disconnect після CONNECTED).
  virtual void onDisconnected(const std::string& ssid) {}

  // Викликається коли всі спроби підключення до конкретної мережі вичерпано.
  virtual void onConnectionFailed(const WifiConnection& conn) {}

  // ---- точка доступу ----

  // Викликається після успішного запуску AP.
  virtual void onApStarted(const std::string& apSsid, const std::string& ip) {}

  // Викликається після зупинки AP.
  virtual void onApStopped() {}
};
