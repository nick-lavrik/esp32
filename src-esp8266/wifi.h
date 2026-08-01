// wifi.h
#pragma once

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include "Display.hpp"
#include "OledColors.hpp"

// --- WiFi / NTP ---
const char* ssid = WIFI_SSID;
const char* password = WIFI_PASSWORD;

extern Display display;

void setupWiFi() {
    display.clear();
    display.setTextColor(TFT_WHITE);
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.print("Connecting WiFi..");
    display.flush();

    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 30) {
        display.print(".");
        display.flush();
        delay(500);
        attempts++;
    }

    if (WiFi.status() == WL_CONNECTED) {
        display.println("\nWiFi connected!");
        display.flush();
        Serial.println("WiFi connected, IP: " + WiFi.localIP().toString());
    } else {
        display.setTextColor(TFT_RED);
        display.println("\nWiFi FAILED");
        display.flush();
        delay(5000);
    }
}

// ESP8266 не має wifi_auth_mode_t/WIFI_AUTH_* (це ESP32 API) - тут ENC_TYPE_* з ESP8266WiFiType.h
String WiFi_getAuthTypeName(uint8_t encryptionType) {
    switch (encryptionType) {
        case ENC_TYPE_NONE:  return String("OPEN");
        case ENC_TYPE_WEP:   return String("WEP");
        case ENC_TYPE_TKIP:  return String("WPA_PSK (TKIP)");
        case ENC_TYPE_CCMP:  return String("WPA2_PSK (CCMP)");
        case ENC_TYPE_AUTO:  return String("AUTO");
        default:             return String("UNKNOWN");
    }
}

// ESP8266 не має WiFi.getNetworkInfo() (це ESP32 API) - тут окремі геттери WiFi.SSID(i)/RSSI(i)/...
void WiFi_scan() {
    Serial.println("Starting full Wi-Fi network scan...");

    int16_t networkCount = WiFi.scanNetworks(false, true);

    if (networkCount == WIFI_SCAN_FAILED) {
        Serial.println("Scan failed!");
    } else if (networkCount == 0) {
        Serial.println("No networks found.");
    } else {
        Serial.printf("\nFound %d networks:\n", networkCount);
        Serial.println("==================================================");

        for (int16_t i = 0; i < networkCount; ++i) {
            String ssidStr = WiFi.SSID(i);
            if (ssidStr.length() == 0) {
                ssidStr = "[Hidden Network]";
            }

            uint8_t* bssid = WiFi.BSSID(i);
            char bssidStr[18];
            snprintf(bssidStr, sizeof(bssidStr), "%02X:%02X:%02X:%02X:%02X:%02X",
                     bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5]);

            Serial.println("SSID:       " + ssidStr);
            Serial.println("BSSID (MAC):" + String(bssidStr));
            Serial.println("Signal:     " + String(WiFi.RSSI(i)) + " dBm");
            Serial.println("Channel:    " + String(WiFi.channel(i)));
            Serial.println("Security:   " + WiFi_getAuthTypeName(WiFi.encryptionType(i)));
            Serial.println("==================================================");
        }
    }

    WiFi.scanDelete();
    Serial.println("Wi-Fi Scan done.");
}
