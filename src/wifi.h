#include <Arduino.h>
#if defined(BOARD_ESP8266)
#include <ESP8266WiFi.h>
#else
#include <WiFi.h>
#endif
#include <Logger.hpp>

// --- WiFi / NTP ---
const char* ssid = WIFI_SSID;
const char* password = WIFI_PASSWORD;

#if ESP32 || defined(BOARD_ESP32_C6)
#include <esp_wifi.h> // Обов'язково додайте цей системний заголовок
#endif

void setupWiFi() {
#if defined(BOARD_ESP8266)
  WiFi.mode(WIFI_STA);
#endif
  // WiFi.setBufferSize(2048, 2048);
  // WiFi.setNoDelay(true);
  WiFi.mode(WIFI_STA);
  #if ESP32 || defined(BOARD_ESP32_C6)
// ВАЖНО: Выключаем Wi-Fi 6 ДО старта подключения, чтобы избежать краша
    esp_err_t err = esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N);
    if (err == ESP_OK) {
        Serial.println("Успешно принудительно включен Wi-Fi 4 (802.11n)");
    } else {
        Serial.printf("Ошибка смены протокола: %d\n", err);
    }
  // esp_wifi_set_ps(WIFI_PS_NONE); 
  #endif
  //  WiFi.setSleep(false);
  WiFi.begin(ssid, password);
  // Modem sleep (power-save) вимкнено для ВСІХ плат: раніше рятувало від
  // "сміття в моніторі" на ttgo-t1, і, ймовірно, причина затримок 2-3с у
  // mqtt.loop()/hostByName() на esp32-c6 (WiFi6-чипи агресивніше
  // присипляють радіо між beacon-інтервалами за замовчуванням).

  /* Serial.print("[WiFi] Connecting");
  while (WiFi.status() != WL_CONNECTED) {
    delay(200);
    Serial.print(".");
  }
  Serial.printf("\n[WiFi] Connected, IP: %s\n", WiFi.localIP().toString().c_str()); */
}

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
      return String("WPA_PSK (TKIP)");
    case ENC_TYPE_CCMP:
      return String("WPA2_PSK (CCMP)");
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
      return String("WPA2_ENTERPRISE");
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

void WiFi_scan() {
  // Переведення Wi-Fi у режим станції та відключення від поточних мереж
  // WiFi.mode(WIFI_STA);
  // WiFi.disconnect();
  // delay(100);

  Logger::info("Starting full Wi-Fi network scan...");

  // Скануємо також і приховані мережі (async = false, show_hidden = true)
  int16_t networkCount = WiFi.scanNetworks(false, true);

  if (networkCount == WIFI_SCAN_FAILED) {
    Logger::warn("Scan failed!");
  } else if (networkCount == 0) {
    Logger::warn("No networks found.");
  } else {
    Logger::info("Found %d networks with detailed parameters:", networkCount);
    Logger::info("==================================================");

    for (int16_t i = 0; i < networkCount; ++i) {
#if defined(BOARD_ESP8266)
      // ESP8266 не має WiFi.getNetworkInfo() (це ESP32 API) - окремі геттери
      String ssid = WiFi.SSID(i);
      uint8_t encryptionType = WiFi.encryptionType(i);
      int32_t rssi = WiFi.RSSI(i);
      uint8_t* bssid = WiFi.BSSID(i);
      int32_t channel = WiFi.channel(i);
#else
      // Змінні для збереження інформації через посилання
      String ssid;
      uint8_t encryptionType;
      int32_t rssi;
      uint8_t* bssid;
      int32_t channel;

      // Витягуємо всі базові дані за один виклик за допомогою вбудованого методу
      WiFi.getNetworkInfo(i, ssid, encryptionType, rssi, bssid, channel);
#endif

      if (ssid.length() == 0) {
        ssid = "[Hidden Network]";
      }

      // Форматуємо BSSID (MAC-адресу) у String формат (AA:BB:CC:DD:EE:FF)
      char bssidStr[18];
      snprintf(bssidStr, sizeof(bssidStr), "%02X:%02X:%02X:%02X:%02X:%02X", bssid[0], bssid[1],
               bssid[2], bssid[3], bssid[4], bssid[5]);

      String signalStr = String(rssi) + String(" dBm");
#if defined(BOARD_ESP8266)
      String securityStr = WiFi_getAuthTypeName(encryptionType);
#else
      String securityStr = WiFi_getAuthTypeName(static_cast<wifi_auth_mode_t>(encryptionType));
#endif
      String channelStr = String(channel);

      // Виведення зібраних String-даних
      Logger::info("SSID:       %s", ssid.c_str());
      Logger::info("BSSID (MAC):%s", bssidStr);
      Logger::info("Signal:     %s", signalStr.c_str());
      Logger::info("Channel:    %s", channelStr);
      Logger::info("Security:   %s", securityStr);
      Logger::info("==================================================");
    }
  }

  // Очищення пам'яті після сканування
  WiFi.scanDelete();
  Logger::info("Wi-Fi Scan done.");
}