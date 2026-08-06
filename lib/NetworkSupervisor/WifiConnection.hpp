#pragma once
#include <stdint.h>
#include <string>

// Одне збережене WiFi-з'єднання.
//
// Використання (через NetworkSupervisor):
//   WifiConnection conn;
//   conn.ssid = "MyNetwork";
//   conn.password = "secret";
//   conn.priority = 10;
//   uint16_t id = nm.addConnection(conn);
//
//   // В INetworkSupervisorListener::onScanResult():
//   for (auto* c : known) {
//     if (c->rssi < -85) c->setEnabled(false); // вимкнути слабкі мережі
//   }
//
// Серіалізація:
//   Об'єкт не містить методів серіалізації — це відповідальність NetworkSupervisor.
//   JSON-ключі відповідають іменам полів (camelCase).

struct WifiConnection {
  // ---- ідентифікація ----
  uint16_t connectionId = 0;  // opaque, призначається NetworkSupervisor при addConnection()
  std::string ssid;
  std::string password;

  // ---- пріоритет і стан ----
  int8_t priority = 0;          // вищий = підключатись раніше (після lastConnected)
  uint32_t lastConnected = 0;   // unix timestamp останнього успішного з'єднання; 0 = ніколи
  bool isEnabled = true;        // false — пропускається при підборі мережі

  // ---- спроби підключення ----
  // -1 = використати NetworkSupervisorConfig::maxRetries
  int8_t maxRetries = -1;

  // ---- статична IP (опціонально) ----
  bool staticIp = false;
  std::string ip;       // "192.168.1.100"
  std::string gateway;  // "192.168.1.1"
  std::string subnet;   // "255.255.255.0"
  std::string dns;      // "8.8.8.8"

  // ---- runtime (не зберігається) ----
  int8_t rssi = 0;      // заповнюється під час onScanResult, не персистується

  // ---- публічні методи зміни стану (для INetworkSupervisorListener) ----
  void setEnabled(bool enabled) { isEnabled = enabled; }
  void setPriority(int8_t p) { priority = p; }
};
