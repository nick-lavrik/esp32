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
