#pragma once

#include <Arduino.h>
#if defined(BOARD_ESP8266)
#include <ESP8266WiFi.h>
#else
#include <WiFi.h>
#endif
#include <Logger.hpp>

#include <string>
#include <vector>

// --- WiFi ---
// Креденшели з build flags (secrets.ini). Це вже НЕ робоча конфігурація
// пристрою, а лише "заводський" запис: NetworkSupervisor засіває ним свій
// список при першому старті (див. setupNetworkSupervisor() у main.cpp), далі
// список живе в NVS і редагується командою 'net'.
const char* ssid = WIFI_SSID;
const char* password = WIFI_PASSWORD;

// Налаштування радіо (WIFI_STA, обмеження до 802.11b/g/n, повний скан по
// каналах) переїхало в NetworkSupervisor::_applyStaRadioConfig(): його треба
// застосовувати перед КОЖНИМ WiFi.begin(), а не один раз на старті, бо після
// AP-циклу режим радіо збивається. Тому setupWiFi() тут більше немає.

#if defined(BOARD_ESP8266)
// ESP8266 не має wifi_auth_mode_t/WIFI_AUTH_* (це ESP32 API) - тут ENC_TYPE_*
// з ESP8266WiFiType.h (те, що повертає WiFi.encryptionType(i) на ESP8266)
String WiFi_getAuthTypeName(uint8_t encryptionType) {
  switch (encryptionType) {
    case ENC_TYPE_NONE:
      return String("OPEN");
    case ENC_TYPE_WEP:
      return String("WEP");
    case ENC_TYPE_TKIP:
      return String("WPA_PSK");
    case ENC_TYPE_CCMP:
      return String("WPA2_PSK");
    case ENC_TYPE_AUTO:
      return String("AUTO");
    default:
      return String("UNKNOWN");
  }
}
// ESP8266 core не надає API для визначення бітмаски протоколів (802.11b/g/n) - немає аналога.
#else
// Функція повертає об'єкт String, використовуючи C++17 string_view для оптимізації
String WiFi_getAuthTypeName(wifi_auth_mode_t authMode) {
  switch (authMode) {
    case WIFI_AUTH_OPEN:
      return String("OPEN");
    case WIFI_AUTH_WEP:
      return String("WEP");
    case WIFI_AUTH_WPA_PSK:
      return String("WPA_PSK");
    case WIFI_AUTH_WPA2_PSK:
      return String("WPA2_PSK");
    case WIFI_AUTH_WPA_WPA2_PSK:
      return String("WPA_WPA2_PSK");
    case WIFI_AUTH_WPA2_ENTERPRISE:
      return String("WPA2_ENT");
    case WIFI_AUTH_WPA3_PSK:
      return String("WPA3_PSK");
    case WIFI_AUTH_WPA2_WPA3_PSK:
      return String("WPA2_WPA3_PSK");
    default:
      return String("UNKNOWN");
  }
}

// Функція для визначення стандарту Wi-Fi за маскою протоколів
String WiFi_getProtocolName(uint8_t protocol_bitmap) {
  String protocols = "";
  if (protocol_bitmap & WIFI_PROTOCOL_11B) protocols += "802.11b ";
  if (protocol_bitmap & WIFI_PROTOCOL_11G) protocols += "802.11g ";
  if (protocol_bitmap & WIFI_PROTOCOL_11N) protocols += "802.11n ";
  if (protocol_bitmap & WIFI_PROTOCOL_LR) protocols += "ESP-LR ";

#ifdef WIFI_PROTOCOL_11AX  // Підтримка Wi-Fi 6 для нових чіпів (ESP32-C6 тощо)
  if (protocol_bitmap & WIFI_PROTOCOL_11AX) protocols += "802.11ax ";
#endif

  if (protocols.length() == 0) return "UNKNOWN";
  protocols.trim();  // Прибираємо зайві пробіли на кінці
  return protocols;
}
#endif

// Скан ефіру у форматі 'nmcli device wifi list'.
//
// knownSsids - SSID зі списку профілів NetworkSupervisor: вони позначаються в
// колонці IN-USE. Раніше тут стояла позначка "збіглося з WIFI_SSID", але після
// переїзду на NetworkSupervisor build-flag більше не описує стан пристрою, і
// така позначка була б просто неправдою.
//
// IN-USE: '*' - поточне з'єднання, '+' - є збережений профіль.
void WiFi_scan(const std::vector<std::string>& knownSsids = {}) {
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
    snprintf(bssidStr, sizeof(bssidStr), "%02X:%02X:%02X:%02X:%02X:%02X", bssid[0], bssid[1],
             bssid[2], bssid[3], bssid[4], bssid[5]);

#if defined(BOARD_ESP8266)
    String securityStr = WiFi_getAuthTypeName(encryptionType);
#else
    String securityStr = WiFi_getAuthTypeName(static_cast<wifi_auth_mode_t>(encryptionType));
#endif

    // .c_str() обов'язково - String через "..." до %s це UB, див.
    // SerialCommander::printUnknown().
    Logger::info("%-6s  %-32s  Infra  %4d  %4d    %-14s %s", inUse,
                 hidden ? "--" : scannedSsid.c_str(), (int)channel, (int)rssi,
                 securityStr.c_str(), bssidStr);
  }

  // Очищення пам'яті після сканування
  WiFi.scanDelete();
  Logger::info("%d network(s), '*' = in use, '+' = saved profile", (int)networkCount);
}
