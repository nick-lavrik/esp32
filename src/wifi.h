#include <WiFi.h>
#include <TFT_eSPI.h>


// --- WiFi / NTP ---
const char* ssid = WIFI_SSID;
const char* password = WIFI_PASSWORD;


void setupWiFi(TFT_eSPI tft) {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(2);
  tft.setCursor(10, 10);
  tft.println("Connecting WiFi...");

  WiFi.begin(ssid, password);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    attempts++;
  }

  // tft.fillScreen(BG_COLOR);
  if (WiFi.status() == WL_CONNECTED) {
    tft.setCursor(10, 10 + 3 + tft.fontHeight());
    tft.println("WiFi connected!");
    Serial.println("WiFi connected (esp32-st7729), IP: " + WiFi.localIP().toString());
    // setLed(false, true, false);

  } else {
    tft.setTextColor(TFT_RED);
    tft.setCursor(10, 10 + 3 + tft.fontHeight());
    tft.println("WiFi FAILED");
    // setLed(true, false, false);
    delay(1000);
  }
}
