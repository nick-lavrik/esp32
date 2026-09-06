#pragma once
#include <stdint.h>
#include <string>

// Метод WPS-підключення.
enum class WpsMethod : uint8_t {
  PBC = 0,  // Push Button Configuration — користувач тисне кнопку на роутері
  PIN = 1,  // PIN-код (8 цифр)
};

// Конфігурація NetworkSupervisor.
//
// Зберігається в ConfigStorage під ключем "nm_config" як JSON.
//
// Використання:
//   NetworkSupervisorConfig cfg;
//   cfg.apSsid     = "ESP32-AP";
//   cfg.apPassword = "12345678";
//   cfg.autoReconnect = true;
//   // WPS як автоматичний fallback перед AP:
//   cfg.wpsEnabled    = true;
//   cfg.wpsMethod     = WpsMethod::PBC;
//   cfg.wpsTimeoutMs  = 120000;
//   ns.setConfig(cfg);
//   ns.saveConfig();

struct NetworkSupervisorConfig {
  // ---- сканування ----
  uint32_t scanIntervalMs = 60000;  // інтервал між скануваннями в AP_MODE / RECONNECTING

  // ---- підключення ----
  uint32_t connectTimeoutMs = 10000;  // таймаут однієї спроби підключення
  uint32_t retryDelayMs = 2000;       // затримка між повторними спробами
  uint8_t maxRetries = 3;             // глобальний default (якщо WifiConnection::maxRetries == -1)
  bool scanBeforeConnect = true;      // true  → scan → filter by visible → connect
                                      // false → перебирати список без попереднього скану

  // ---- налаштування радіо STA ----
  // Обмежити станцію 802.11b/g/n ДО старту конекту: на чипах з Wi-Fi 6
  // (C6) асоціація з AX-точкою валила стек. На чипах без AX (класичний
  // ESP32, S3, C3) це фактично no-op - там така бітмаска і так дефолтна.
  bool forceWifi4 = true;

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
  bool fullChannelScan = true;

  // ---- автоперепідключення ----
  bool autoReconnect = true;  // false → нічого не робить після втрати; потрібен ручний виклик

  // ---- WPS ----
  bool wpsEnabled = false;           // увімкнути WPS як крок FSM після невдалих з'єднань
  WpsMethod wpsMethod = WpsMethod::PBC;
  uint32_t wpsTimeoutMs = 120000;    // таймаут очікування WPS (default 120 сек)
  std::string wpsPin = "";           // PIN для WpsMethod::PIN (8 цифр)
                                     // якщо порожній — пристрій генерує і передає через
                                     // onWpsPinGenerated(); роутер вводить цей PIN
  bool wpsSaveOnSuccess = true;      // зберегти отриману мережу в список з'єднань
  int8_t wpsSavedPriority = 0;       // пріоритет для збереженого WPS-з'єднання

  // ---- точка доступу (AP fallback) ----
  // false → коли жодного кандидата не знайдено, AP НЕ піднімається;
  // FSM просто продовжує сканувати з інтервалом scanIntervalMs.
  bool apFallbackEnabled = true;

  std::string apSsid = "ESP-NetworkSupervisor";
  std::string apPassword = "";  // порожній рядок → відкрита мережа
  uint8_t apChannel = 1;
  std::string apIp = "192.168.4.1";
};
