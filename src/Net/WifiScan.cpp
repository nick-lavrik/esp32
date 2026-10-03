#include "WifiScan.hpp"

#include <Arduino.h>
#if defined(BOARD_ESP8266)
#include <ESP8266WiFi.h>
#else
#include <WiFi.h>
#endif
#include <Logger.hpp>
#include <NetworkSupervisor.hpp>

void wifiScan(const std::vector<std::string>& knownSsids) {
  Logger::info("Starting full Wi-Fi network scan...");

  // Скануємо також і приховані мережі (async = false, show_hidden = true)
  int16_t networkCount = WiFi.scanNetworks(false, true);

  if (networkCount == WIFI_SCAN_FAILED) {
    Logger::warn("Scan failed (another scan already running?)");
    return;
  }
  if (networkCount == 0) {
    Logger::warn("No networks found.");
    return;
  }

  const String activeSsid = WiFi.isConnected() ? WiFi.SSID() : String("");

  Logger::info("IN-USE  SSID                              MODE   CHAN  SIGNAL  SECURITY       BSSID");

  for (int16_t i = 0; i < networkCount; ++i) {
#if defined(BOARD_ESP8266)
    // ESP8266 не має WiFi.getNetworkInfo() (це ESP32 API) - окремі геттери
    String scannedSsid = WiFi.SSID(i);
    uint8_t encryptionType = WiFi.encryptionType(i);
    int32_t rssi = WiFi.RSSI(i);
    uint8_t* bssid = WiFi.BSSID(i);
    int32_t channel = WiFi.channel(i);
#else
    // Витягуємо всі базові дані за один виклик за допомогою вбудованого методу
    String scannedSsid;
    uint8_t encryptionType;
    int32_t rssi;
    uint8_t* bssid;
    int32_t channel;
    WiFi.getNetworkInfo(i, scannedSsid, encryptionType, rssi, bssid, channel);
#endif

    const bool hidden = (scannedSsid.length() == 0);

    const char* inUse = " ";
    if (!hidden && activeSsid.length() > 0 && scannedSsid == activeSsid) {
      inUse = "*";
    } else if (!hidden) {
      for (const auto& known : knownSsids) {
        if (scannedSsid == known.c_str()) {
          inUse = "+";
          break;
        }
      }
    }

    char bssidStr[18];
    snprintf(bssidStr, sizeof(bssidStr), "%02X:%02X:%02X:%02X:%02X:%02X", bssid[0], bssid[1], bssid[2], bssid[3],
             bssid[4], bssid[5]);

    const char* securityStr = wifiAuthTypeName(encryptionType);

    // .c_str() обов'язково - String через "..." до %s це UB, див.
    // SerialCommander::printUnknown().
    Logger::info("%-6s  %-32s  Infra  %4d  %4d    %-14s %s", inUse, hidden ? "--" : scannedSsid.c_str(), (int)channel,
                 (int)rssi, securityStr, bssidStr);
  }

  // Очищення пам'яті після сканування
  WiFi.scanDelete();
  Logger::info("%d network(s), '*' = in use, '+' = saved profile", (int)networkCount);
}
