#include "SystemStatus.hpp"

#include <LittleFS.h>

#include <EspPartitionInspector.hpp>
#include <Logger.hpp>
#include <SystemReset.hpp>
#include <TLogger.hpp>

#include "App/AppGlobals.hpp"
#include "Sd/Sd.hpp"
#include "SizeFormatter.hpp"

#if defined(ESP8266)
#include <ESP8266WiFi.h>
#else
#include <WiFi.h>
#endif
#if defined(ESP32)
#include <esp_heap_caps.h>
#endif

void dumpSystemInfo() {
  Logger::info("======== ESP32 CHIP INFO ==================================");

  // --- PlatformIO environment ---
  Logger::info("PlatformIO: %s", PIO_PIOENV);

// --- Модель чипа ---
#if defined(BOARD_ESP8266)
  Logger::info("Chip: ESP8266 (chipId=0x%06X)", ESP.getChipId());
  Logger::info("CPU freq: %d MHz", ESP.getCpuFreqMHz());
#else
  Logger::info("Chip model: %s", ESP.getChipModel());
  Logger::info("Chip revision: %d", ESP.getChipRevision());
  Logger::info("CPU cores: %d", ESP.getChipCores());
  Logger::info("CPU freq: %d MHz", ESP.getCpuFreqMHz());
#endif

  //  возвращает общее количество тактов процессора (CPU cycles), прошедших с момента запуска
  // Logger::info("Cycle Count: %d", ESP.getCycleCount());

  // --- ESP-IDF ---
  Logger::info("SDK version:  %s", ESP.getSdkVersion());
#if defined(BOARD_ESP8266)
  Logger::info("Core version: %s", ESP.getCoreVersion().c_str());  // на ESP8266 core - String
#else
  Logger::info("Core version: %s", ESP.getCoreVersion());
#endif

  // --- Flash ---
  Logger::info("Flash size:  %d bytes (%.2f MB)", ESP.getFlashChipSize(), ESP.getFlashChipSize() / 1024.0 / 1024.0);
  Logger::info("Flash speed: %d Hz", ESP.getFlashChipSpeed());

// --- Внутрішня RAM (SRAM) ---
#if defined(BOARD_ESP8266)
  Logger::info("Free heap:   %d bytes", ESP.getFreeHeap());
// ESP8266 не має getHeapSize()/PSRAM - пропускаємо
#else
  Logger::info("Total heap:  %d bytes", ESP.getHeapSize());
  Logger::info("Free heap:   %d bytes", ESP.getFreeHeap());

  // --- PSRAM ---
  Logger::info("PSRAM found: %s", psramFound() ? "YES" : "NO");
  if (psramFound()) {
    Logger::info("Total PSRAM: %d bytes (%.2f Mb)", ESP.getPsramSize(), ESP.getPsramSize() / 1024.0 / 1024.0);
    // Було: два специфікатори на ОДИН аргумент, до того ж double під %d.
    Logger::info("Free PSRAM:  %d bytes (%.2f Mb)", ESP.getFreePsram(), ESP.getFreePsram() / 1024.0 / 1024.0);
  }
#endif
  /*
  Шпаргалка: як розуміти значення dBm
  Оскільки значення RSSI від'ємні, чим ближче воно до нуля, тим кращий сигнал:
  - від -30 до -50 dBm — Ідеальний сигнал (мікроконтролер стоїть впритул до роутера).
  - від -60 до -67 dBm — Хороший, стабільний сигнал (достатній для передачі великих обсягів даних чи потокового відео).
  - від -70 до -80 dBm — Слабкий сигнал (працювати буде, але можливі затримки або втрата пакетів).
  - -90 dBm і гірше — Критичний рівень (зв'язок постійно обриватиметься).
  */

  Logger::info("");
  uint32_t uptimeSec = millis() / 1000;
  Logger::info("Uptime: %02u:%02u:%02u", (unsigned)(uptimeSec / 3600), (unsigned)((uptimeSec / 60) % 60),
               (unsigned)(uptimeSec % 60));

  Logger::info("WiFi SSID: %s (%d dBm / %d%%)", WiFi.SSID().c_str(), WiFi.RSSI(), wifiSignalQuality(WiFi.RSSI()));
  if (WiFi.status() == WL_CONNECTED) {
    Logger::info("WiFi   IP: %s", WiFi.localIP().toString().c_str());
  } else {
    Logger::info("WiFi disconnected....");
  }
  Logger::info("Last Reset Reason: %s", SystemReset::getLastResetReason());
  Logger::info("display.brightness = %d", display.brightness());
  /*
  Logger::info("======= ESP32 HEAP INFO ========");
  heap_caps_print_heap_info(MALLOC_CAP_DEFAULT); // друкує все одразу у форматованому вигляді
  */
  Logger::info("============================================================");
}

static void dumpConfigStorage() {
  Logger::info("====== ConfigStorage (NVS) =================================");
  auto entries = configStorage.listEntries();

  if (entries.empty()) {
    Logger::info("(empty.)");
  }

  // Форматування значення за типом живе в ConfigStorage::getAsString(): той
  // самий рядок показує і веб-редактор NVS (WebNvsModule), а тримати два
  // switch'и на десяток типів - рівно те дублювання, що розходиться першим.
  for (const auto& e : entries) {
    Logger::info("  key: %-16s type: %-4s value: %s", e.key.c_str(), e.typeName.c_str(),
                 configStorage.getAsString(e.key.c_str(), e.type).c_str());
  }

  Logger::info("");
  Logger::info("Total records: %d", entries.size());
  Logger::info("============================================================");
}

static void dumpLittleFSInfo() {
  Logger::info("========= LittleFS INFO ====================================");

// --- Список усіх файлів ---
#if defined(ESP8266)
  Dir root = LittleFS.openDir("/");
  while (root.next()) {
    Logger::info("File: %-28s %8d bytes (%s)", root.fileName().c_str(), root.fileSize(),
                 SizeFormatter::format(root.fileSize()).c_str());
  }
#else
  File root = LittleFS.open("/");
  File file = root.openNextFile();
  while (file) {
    Logger::info("File: %-28s %8d bytes (%s)", file.name(), file.size(), SizeFormatter::format(file.size()).c_str());
    file = root.openNextFile();
  }
#endif

#if defined(ESP8266)
  FSInfo64 fsInfo64;
  LittleFS.info64(fsInfo64);
  int usedBytes = fsInfo64.usedBytes;
  int totalBytes = fsInfo64.totalBytes;
  double freePercent = ((totalBytes - usedBytes) * 100.00 / totalBytes);
#else
  size_t usedBytes = LittleFS.usedBytes();
  size_t totalBytes = LittleFS.totalBytes();
  double freePercent = ((totalBytes - usedBytes) * 100.00 / totalBytes);
#endif

  // --- Скільки місця залишилось ---
  Logger::info("");
  Logger::info("Used: %d / Total: %d / Free: %d bytes | Free: %.3f%%", usedBytes, totalBytes, totalBytes - usedBytes,
               freePercent);
  Logger::info("============================================================");
}

void dumpStatus(const String& section) {
  static TLogger logger("flash");

  if (section.equals("sys")) {
    dumpSystemInfo();
  } else if (section.equals("cfg")) {
    dumpConfigStorage();
  } else if (section.equals("littlefs")) {
    dumpLittleFSInfo();
  } else if (section.equals("flash")) {
    EspPartitionInspector::printAll(logger);
  } else if (section.equals("flash+")) {
    EspPartitionInspector::printAll(logger, true);
#if BOARD_HAS_SD
  } else if (section.equals("sd")) {
    printSdStatus();
  } else if (section.equals("sd+")) {
    dumpSDInfo();
#endif
  } else {
    Logger::warn("use: status sys|cfg|sd|sd+|flash|flash+|littlefs");
  }
}

void registerSystemStatusCommands(SerialCommander& commander) {
  // Фрагментація важливіша за сам обсяг вільного heap: алокація падає, коли
  // немає ОДНОГО суцільного блоку потрібного розміру, навіть якщо сумарно
  // вільно вдесятеро більше. largest/free і є цим показником.
  commander.registerCommand("heap", "show heap usage and fragmentation", [](const String args) {
    static TLogger _log{"heap"};
#if defined(ESP32)
    // heap_caps_* - ESP-IDF API, на ESP8266 його немає (див. #else).
    const size_t total = heap_caps_get_total_size(MALLOC_CAP_8BIT);
    const size_t freeNow = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    const size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    const size_t minEver = heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT);

    _log.info("total        : %u B", (unsigned)total);
    _log.info("free         : %u B (%u%% of total)", (unsigned)freeNow, total ? (unsigned)(freeNow * 100 / total) : 0);
    _log.info("largest block: %u B", (unsigned)largest);
    _log.info("fragmentation: %u%%  (free minus largest = %u B in holes)",
              freeNow > 0 ? (unsigned)(100 - (largest * 100) / freeNow) : 0, (unsigned)(freeNow - largest));
    _log.info("min free ever: %u B  <- worst moment since boot", (unsigned)minEver);
#else
    // ESP8266 SDK дає готовий відсоток фрагментації, але не знає ні загального
    // розміру купи, ні історичного мінімуму.
    const size_t freeNow = ESP.getFreeHeap();
    const size_t largest = ESP.getMaxFreeBlockSize();
    _log.info("free         : %u B", (unsigned)freeNow);
    _log.info("largest block: %u B", (unsigned)largest);
    _log.info("fragmentation: %u%%  (free minus largest = %u B in holes)",
              (unsigned)ESP.getHeapFragmentation(), (unsigned)(freeNow - largest));
#endif
    const unsigned long uptimeSec = millis() / 1000UL;
    const unsigned long uptimeDays = uptimeSec / 86400UL;
    const unsigned long uptimeHours = (uptimeSec % 86400UL) / 3600UL;
    const unsigned long uptimeMins = (uptimeSec % 3600UL) / 60UL;
    const unsigned long uptimeSecs = uptimeSec % 60UL;
    _log.info("uptime       : %lu s  (%lud %02luh %02lum %02lus)", uptimeSec, uptimeDays, uptimeHours, uptimeMins,
              uptimeSecs);
  });

  // Постійний інструмент діагностики heap (docs/tech_debt.md, розділ 4,
  // "esp32-c3 - той самий конфлікт, лише повільніший крах"): портал+EcoFlow
  // тут заганяють heap в той самий кут, що й на ttgo-t1/esp32-st7789, тож
  // моніторинг лишається - знадобиться перевіряти кожну спробу це полікувати.
  // commandQueue.submit(), а не прямий виклик - той самий шлях, що й у консолі/MQTT,
  // тож результат однаково потрапляє в journal і, за потреби, у console-mqtt.
  const TaskId heapWatchCronTaskId = scheduler.addCronTask(2 * 60 * 1000UL, []() {
    static TLogger _log{"heap"};
    if (!commandQueue.submit("heap")) _log.warn("command queue full, cron 'heap' skipped");
  });

  // command: heap-watch - та сама схема вимикача, що й ecoflow-watch вище:
  // pause/resume таска + збереження стану в NVS, щоб пережити перезапуск.
  commander.registerCommand("heap-watch", "periodic 'heap' sampling: heap-watch [on|off]",
                            [heapWatchCronTaskId](const String args) {
                              static TLogger _log{"heap"};
                              if (args.equalsIgnoreCase("on")) {
                                scheduler.resume(heapWatchCronTaskId);
                                configStorage.setBool(CFG_HEAP_WATCH, true);
                              } else if (args.equalsIgnoreCase("off")) {
                                scheduler.pause(heapWatchCronTaskId);
                                configStorage.setBool(CFG_HEAP_WATCH, false);
                              } else if (args.length() != 0) {
                                _log.info("use: heap-watch [on|off]");
                                return;
                              }
                              _log.info("heap watch - %s", scheduler.isPaused(heapWatchCronTaskId) ? "off" : "on");
                            });

  // heap-watch увімкнений за замовчуванням (те саме, що ecoflow-watch) - вимкнення
  // застосовується одразу при старті, якщо збережене в NVS.
  if (!configStorage.getBool(CFG_HEAP_WATCH, true)) {
    scheduler.pause(heapWatchCronTaskId);
    static TLogger _log{"heap"};
    _log.warn("heap watch disabled");
  }

  commander.registerCommand("status", "show device status state: status sys|cfg|sd|sd+|flash|flash+|littlefs",
                            [](const String& args) { dumpStatus(args); });
}
