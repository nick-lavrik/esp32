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

// УВАГА: тут НЕ "#if ESP32". arduino-esp32 визначає цей макрос самопосилально
// (-DESP32=ESP32, див. pioarduino-build.py), а такий ідентифікатор у #if
// розкривається сам у себе й дає 0. Тобто "#if ESP32" тут завжди БУВ false,
// і весь блок нижче працював лише завдяки явним defined(BOARD_ESP32_C6*) -
// на решті ESP32-плат він мовчки не існував. Перевірка на ESP8266 - єдина
// правильна: esp_wifi.h є в усій родині ESP32.
#if !defined(BOARD_ESP8266)
#include <esp_wifi.h>
#endif

void setupWiFi() {
#if defined(BOARD_ESP8266)
  WiFi.mode(WIFI_STA);
#endif
  // WiFi.setBufferSize(2048, 2048);
  // WiFi.setNoDelay(true);
  WiFi.mode(WIFI_STA);
  #if !defined(BOARD_ESP8266)
  // Обмежуємо станцію 802.11b/g/n ДО старту конекту: на чипах з Wi-Fi 6
  // (C6) асоціація з AX-точкою валила стек. На чипах без AX (класичний
  // ESP32, S3, C3) це фактично no-op - там така бітмаска і так дефолтна, -
  // але тримаємо однаково для всіх, щоб поведінка не залежала від плати.
    esp_err_t err = esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N);
    if (err == ESP_OK) {
        Logger::info("Wi-Fi4 (802.11n) forced successfully");
    } else {
        Logger::error("Wi-Fi4 (802.11n) Protocol change error: %d", err);
    }
  // esp_wifi_set_ps(WIFI_PS_NONE); 

  // Повний скан по всіх каналах замість дефолтного WIFI_FAST_SCAN.
  //
  // Fast scan зупиняється на ПЕРШІЙ точці з потрібним SSID і слухає кожен
  // канал дуже коротко - при слабкому сигналі beacon просто не встигає
  // потрапити у вікно, і WiFi.begin() віддає reason 201 (NO_AP_FOUND) на
  // мережу, яку окремий WiFi.scanNetworks() бачить без проблем (той слухає
  // канал довше). Повний скан цю гонку прибирає.
  //
  // Побічний плюс для AiMesh/кількох AP з одним SSID: sort by signal
  // обирає найсильніший BSSID, а не перший-ліпший.
  WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);
  WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);
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

#if defined(BOARD_ESP8266)
      String securityStr = WiFi_getAuthTypeName(encryptionType);
#else
      String securityStr = WiFi_getAuthTypeName(static_cast<wifi_auth_mode_t>(encryptionType));
#endif

      Logger::info("SSID:       \"%s\"%s", ssid.c_str(), ssid.equals(WIFI_SSID) ? " *" : "");
      Logger::info("BSSID (MAC):%s", bssidStr);
      Logger::info("Signal:     %d dBm", rssi);
      Logger::info("Channel:    %d", channel);
      // .c_str() обов'язково - String через "..." до %s це UB, див.
      // SerialCommander::printUnknown().
      Logger::info("Security:   %s", securityStr.c_str());
      Logger::info("==================================================");
    }
  }

  // Очищення пам'яті після сканування
  WiFi.scanDelete();
  Logger::info("Wi-Fi Scan done.");
}
