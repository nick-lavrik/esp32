#include <Arduino.h>
#include <WiFi.h>
#include "Display.h"

// --- WiFi / NTP ---
const char* ssid = WIFI_SSID;
const char* password = WIFI_PASSWORD;

extern Display display;

void setupWiFi() {
  display.clear();
  display.setTextColor(TFT_WHITE);
  display.setTextSize(2);
  display.setCursor(10, 10);
  display.print("Connecting WiFi..");
  display.flush();
  
  WiFi.begin(ssid, password);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    display.print("."); display.flush();
    delay(500);
    attempts++;
  }

  // tft.fillScreen(BG_COLOR);
  if (WiFi.status() == WL_CONNECTED) {
    display.setCursor(10, 10 + 3 + display.fontHeight());
    display.println("WiFi connected!");
    display.flush();
    Serial.println("WiFi connected, IP: " + WiFi.localIP().toString());
    // setLed(false, true, false);
  } else {
    display.setTextColor(TFT_RED);
    display.setCursor(10, 10 + 3 + display.fontHeight());
    display.println("WiFi FAILED");
    display.flush();
    // setLed(true, false, false);
    delay(1000);
  }
}

// Функція повертає об'єкт String, використовуючи C++17 string_view для оптимізації
String WiFi_getAuthTypeName(wifi_auth_mode_t authMode) {
    switch (authMode) {
        case WIFI_AUTH_OPEN:            return String("OPEN");
        case WIFI_AUTH_WEP:             return String("WEP");
        case WIFI_AUTH_WPA_PSK:         return String("WPA_PSK");
        case WIFI_AUTH_WPA2_PSK:        return String("WPA2_PSK");
        case WIFI_AUTH_WPA_WPA2_PSK:    return String("WPA_WPA2_PSK");
        case WIFI_AUTH_WPA2_ENTERPRISE: return String("WPA2_ENTERPRISE");
        case WIFI_AUTH_WPA3_PSK:        return String("WPA3_PSK");
        case WIFI_AUTH_WPA2_WPA3_PSK:   return String("WPA2_WPA3_PSK");
        default:                        return String("UNKNOWN");
    }
}

// Функція для визначення стандарту Wi-Fi за маскою протоколів
String WiFi_getProtocolName(uint8_t protocol_bitmap) {
    String protocols = "";
    if (protocol_bitmap & WIFI_PROTOCOL_11B)  protocols += "802.11b ";
    if (protocol_bitmap & WIFI_PROTOCOL_11G)  protocols += "802.11g ";
    if (protocol_bitmap & WIFI_PROTOCOL_11N)  protocols += "802.11n ";
    if (protocol_bitmap & WIFI_PROTOCOL_LR)   protocols += "ESP-LR ";
    
    #ifdef WIFI_PROTOCOL_11AX // Підтримка Wi-Fi 6 для нових чіпів (ESP32-C6 тощо)
    if (protocol_bitmap & WIFI_PROTOCOL_11AX) protocols += "802.11ax ";
    #endif

    if (protocols.length() == 0) return "UNKNOWN";
    protocols.trim(); // Прибираємо зайві пробіли на кінці
    return protocols;
}

void WiFi_scan() {
    // Переведення Wi-Fi у режим станції та відключення від поточних мереж
    // WiFi.mode(WIFI_STA);
    // WiFi.disconnect();
    // delay(100);

    Serial.println("Starting full Wi-Fi network scan...");

    // Скануємо також і приховані мережі (async = false, show_hidden = true)
    int16_t networkCount = WiFi.scanNetworks(false, true);

    if (networkCount == WIFI_SCAN_FAILED) {
        Serial.println("Scan failed!");
    } else if (networkCount == 0) {
        Serial.println("No networks found.");
    } else {
        Serial.printf("\nFound %d networks with detailed parameters:\n", networkCount);
        Serial.println("==================================================");

        for (int16_t i = 0; i < networkCount; ++i) {
            // Змінні для збереження інформації через посилання
            String ssid;
            uint8_t encryptionType;
            int32_t rssi;
            uint8_t* bssid;
            int32_t channel;

            // Витягуємо всі базові дані за один виклик за допомогою вбудованого методу
            WiFi.getNetworkInfo(i, ssid, encryptionType, rssi, bssid, channel);

            if (ssid.length() == 0) {
                ssid = "[Hidden Network]";
            }

            // Форматуємо BSSID (MAC-адресу) у String формат (AA:BB:CC:DD:EE:FF)
            char bssidStr[18];
            snprintf(bssidStr, sizeof(bssidStr), "%02X:%02X:%02X:%02X:%02X:%02X",
                     bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5]);

            String signalStr = String(rssi) + " dBm";
            String securityStr = WiFi_getAuthTypeName(static_cast<wifi_auth_mode_t>(encryptionType));
            String channelStr = String(channel);

            // Виведення зібраних String-даних
            Serial.println("SSID:       " + ssid);
            Serial.println("BSSID (MAC):" + String(bssidStr));
            Serial.println("Signal:     " + signalStr);
            Serial.println("Channel:    " + channelStr);
            Serial.println("Security:   " + securityStr);
            Serial.println("==================================================");
        }
    }

    // Очищення пам'яті після сканування
    WiFi.scanDelete();
    Serial.println("Wi-Fi Scan done.");
}