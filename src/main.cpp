// main.cpp
//
// esptool --port /dev/ttyUSB0 --after hard-reset chip-id
// python3 -m serial.tools.miniterm --echo --non-exclusive /dev/ttyUSB0 115200
//
// Працює однаково для обох середовищ, різниться лише build_flags (-include)
// у platformio.ini:
//   env:esp32-st7789      -> include/Setup_ST7789.h        (bodmer/TFT_eSPI, SPI)
//   env:esp32-4848s040    -> include/Setup_ST7701_4848S040.h (LovyanGFX, RGB-панель)

// static const uint32_t freqs[] = {150, 300, 500, 800, 1000, 2000};
// for (uint32_t f : freqs) {
//     Serial.printf("Testing freq = %u Hz\n", f);
//     // На жаль, LGFX Light_PWM не дає змінити freq в рантаймі без
//     // повторної ініціалізації - тому цей тест краще робити,
//     // міняючи light_cfg.freq в Setup_ST7701_4848S040.h і перепрошиваючи,
//     // а не в рантаймі.
// }
// Serial.println(F("❌ Fail Message"));
// Serial.println(F("✅ Success Message"));

// ===== ESP32 CHIP INFO =====
// PlatformIO: esp32-4848s040
// Chip model: ESP32-S3
// Chip revision: 2
// CPU cores: 2
// CPU freq: 240 MHz
// SDK version:  v5.5.4
// Core version: 3.3.9
// ===== ESP32 CHIP INFO =====
// PlatformIO: esp32-st7789
// Chip model: ESP32-D0WD-V3
// Chip revision: 301
// CPU cores: 2
// CPU freq: 240 MHz
// SDK version:  v5.5.4
// Core version: 3.3.9

#include <Arduino.h>
#include <SPI.h>

#include "Display.h"
#if BOARD_HAS_SD
#if defined(BOARD_ESP32_S3_LCD147)
// Ця плата підключає TF-карту через SD_MMC (4-bit: D0/D1/D2/D3/CLK/CMD),
// а не через SPI (CS/MOSI/MISO/SCK), як інші плати проєкту.
//
// УВАГА: підміна через "#define SD SD_MMC" тут навмисно НЕ використовується —
// вона ламає компіляцію, бо <SD.h> транзитивно підключають і інші бібліотеки
// (напр. ESP Mail Client -> MB_FS.h), де глобальна текстова підміна імені SD
// конфліктує з їхніми власними деклараціями/викликами SD.*. Замість цього
// нижче явно використовується SD_MMC (SDCardInspector::printAll(SD_MMC, ...),
// SD_MMC.cardType()/cardSize()/... — той самий публічний API, що й fs::SDFS).
#include <SD_MMC.h>
#else
#include <SD.h>
#include <SDCardInspector.hpp>
#endif
#endif
#include <LittleFS.h>
#include <PubSubClient.h>
#include <TouchScreenConfig.h>

#include <AnalogSensor.hpp>
#include <ConfigStorage.hpp>
#include <EspPartitionInspector.hpp>
#include <EventDispatcher.hpp>
#include <GmailSender.hpp>
#include <HttpServer.hpp>
#include <JpegImage.hpp>
#include <LittleFsStaticSource.hpp>
#include <Logger.hpp>
#include <MqttClient.hpp>
#include <NtpService.hpp>
#include <PrintQueue.hpp>
#include <RwLock.hpp>
#include <SerialCommander.hpp>
#include <SystemReset.hpp>
#include <TaskController.hpp>
#include <NetworkSupervisor.hpp>
#include <RouterApiClient.hpp>
#include <RouterClientListParser.hpp>
#include <RouterClientListIterator.hpp>

#include "ScreenLogTail.hpp"

#include "BackgroundImages.hpp"
#include "SizeFormatter.hpp"
#include "ntp.h"
#include "ping.h"
#include "setup.h"
#include "wifi.h"

#if BOARD_HAS_TOUCHSCREEN
#include <TouchController.h>
#endif

const char* EVT_REBOOT = "reboot";
const char* CFG_SHOW_CLOCK = "clock";
const char* CFG_BLINK_LED = "blink";  // ESP8266 BLINK_LED_PIN dependency
const char* CFG_SYS_AUTOBRIGHTNESS = "auto-brightness";
const char* CFG_DISPLAY_BRIGHTNESS = "brightness";

TouchScreenConfig makeTouchScreenConfig() {
  TouchScreenConfig c;
  // Приклад: контролер видає сирі 0..4095, екран фізично 320x240,
  // а сама панель ще й повернута (типова ситуація для дешевих SPI TFT).
  // c.rawMinX = 200;  c.rawMaxX = 3900; // підбирається калібруванням
  // c.rawMinY = 200;  c.rawMaxY = 3900;

#ifdef BOARD_ST7789
  c.rawMinX = 212;
  c.rawMaxX = 3714;
  c.rawMinY = 329;
  c.rawMaxY = 3817;

  c.screenWidth = 320;
  c.screenHeight = 240;

  c.invertY = true;  // якщо вертикаль перевернута
  c.invertX = true;  // якщо горизонталь перевернута
  c.swapXY = false;  // якщо екран повернутий на 90/270 градусів

  c.edgeZoneX = 25;
  c.edgeZoneY = 25;
#endif

#ifdef BOARD_4848S040
  c.rawMinX = 0;
  c.rawMaxX = 480;
  c.rawMinY = 0;
  c.rawMaxY = 480;

  c.screenWidth = 480;
  c.screenHeight = 480;

  c.invertX = false;
  c.invertY = true;
  c.swapXY = true;

  c.edgeZoneX = 40;
  c.edgeZoneY = 40;
#endif

#ifdef BOARD_ESP8266
  c.screenWidth = 128;
  c.screenHeight = 64;
#endif

  return c;
}
MqttConfig makeMqttConfig() {
  MqttConfig config;
  config.host = MQTT_HOST, config.port = MQTT_PORT, config.clientId = MQTT_CLIENT_ID;
  config.username = MQTT_USERNAME;
  config.password = MQTT_PASSWORD;
  config.lwtTopic = MQTT_LWT_TOPIC;
  config.lwtOfflineMessage = MQTT_LWT_MSG_OFFLINE;
  config.lwtOnlineMessage = MQTT_LWT_MSG_ONLINE;

  return config;
}

bool showClock = true;
bool isAutoBrightness = false;

NtpService ntp;
EventDispatcher dispatcher;
TaskController scheduler;
ConfigStorage configStorage;
JpegImage spaceImage;
SerialCommander commandHandler;
WiFiClient wifiClient;
PubSubClient client(wifiClient);
MqttClient mqtt(makeMqttConfig());

LittleFsStaticSource littleFsSource(LittleFS);
HttpServer httpServer(HttpServerConfig{});
//NetworkSupervisor wifi;

RouterApiClient routerApi("192.168.28.1", "QWRtaW46cGFzcw==");

#if BOARD_HAS_DISPLAY
Display display;
TouchScreenConfig displayConfig = makeTouchScreenConfig();
#endif

#if BOARD_HAS_TOUCHSCREEN
TouchPointMapper mapper(displayConfig);
TouchEvents touch(displayConfig);
TouchController touchController;
#endif

#if HAS_GMAIL_SENDER
GmailSender mailer(GMAIL_EMAIL, GMAIL_PASSWORD, "ESP32 Device");
#endif

#if LIGHT_SENSOR_PIN > 0
AnalogSensor lightSensor(LIGHT_SENSOR_PIN, 0, 1855, 100, 0, 5);
#endif

#if BOARD_HAS_TOUCHSCREEN
void onTouchLog(TouchPoint p) { Logger::debug("Touch: %d, %d", p.x, p.y); }
void onHoldHandler(TouchPoint p, unsigned long ms) { Logger::debug("Hold at %d,%d for %lu ms", p.x, p.y, ms); }
void onDblClickHandler(TouchPoint p) { Logger::debug("Double click: %d, %d\n", p.x, p.y); }

void onSwipeLeftHandler(TouchPoint start, TouchPoint end) { Logger::debug("Swipe LEFT"); }
void onSwipeRightHandler(TouchPoint start, TouchPoint end) { Logger::debug("Swipe RIGHT"); }
void onSwipeUpHandler(TouchPoint start, TouchPoint end) { Logger::debug("Swipe UP"); }
void onSwipeDownHandler(TouchPoint start, TouchPoint end) { Logger::debug("Swipe DOWN"); }

void onSwipeFromBottomHandler(TouchPoint start, TouchPoint end) {
  Logger::debug("Swipe FROM BOTTOM (напр., відкрити меню)");
}
void onSwipeFromTopHandler(TouchPoint start, TouchPoint end) {
  Logger::debug("Swipe FROM TOP (напр., шторка сповіщень)");
}
void onSwipeFromLeftHandler(TouchPoint start, TouchPoint end) { Logger::debug("Swipe FROM LEFT (напр., назад)"); }
void onSwipeFromRightHandler(TouchPoint start, TouchPoint end) {
  Logger::debug("Swipe FROM RIGHT (напр., бокова панель)");
}

void onHoldDrawPoints(TouchPoint p, unsigned long ms) {
  // TODO: restore brightness before trigger autobrightness = off (!)
  // display.autobrightness(true);

  // Тип 2 (JobTask): "показувати frame"
  // постійно протягом 10 секунд, після чого само зникає з черги
  scheduler.addJob(
      10UL * 1000UL,
      [p]() {
        display.drawCircle(p.x, p.y, 4, TFT_YELLOW);
        display.drawRect(0, 0, 1, 1, TFT_WHITE);
        display.drawRect(display.width() - 1, 0, 1, 1, TFT_WHITE);
        display.drawRect(display.width() - 1, display.height() - 1, 1, 1, TFT_WHITE);
        display.drawRect(0, display.height() - 1, 1, 1, TFT_WHITE);

        display.drawRect(displayConfig.edgeZoneX, displayConfig.edgeZoneY,
                         displayConfig.screenWidth - 2 * displayConfig.edgeZoneX,
                         displayConfig.screenHeight - 2 * displayConfig.edgeZoneY, TFT_DARKGREY);
      },
      1  // з інтервалом 1 мілісекунда, а не на кожному tick()
  );

  Logger::info(" ------ !!! ONHOLD FRAME !!! ------ ");
}
#endif

void display_brightness(uint8_t percent, bool _auto) {
  display.brightness(percent);
  configStorage.setInt(CFG_DISPLAY_BRIGHTNESS, display.brightness());
  configStorage.setBool(CFG_SYS_AUTOBRIGHTNESS, isAutoBrightness = _auto);
  Logger::info("display.brightness(%d)%s", display.brightness(), isAutoBrightness ? " (auto)" : "");
}

void display_flip() {
  displayConfig.invertY = !displayConfig.invertY;
  displayConfig.invertX = !displayConfig.invertX;
  display.flip();
}

void show_clock(bool show) {
  configStorage.setBool(CFG_SHOW_CLOCK, showClock = show);
  Logger::debug("showClock = %s", showClock ? "YES" : "NO");
}

void setupTouchScreen() {
#if BOARD_HAS_TOUCHSCREEN
  touch.setTouchPointMapper(&mapper);
  touchController.setup(&touch);
  Logger::debug("TouchScreen setup done");

  touchController.events().onHold(onHoldDrawPoints);

  touchController.events().onSwipeUp([](TouchPoint s, TouchPoint e) {
    if (display.brightness() == 0) {
      display_brightness(1, false);
    } else if (display.brightness() == 1) {
      display_brightness(10, false);
    } else {
      display_brightness(min(100, display.brightness() + 10), false);
    }
    Logger::debug("Brightness: %d%% (increase)", display.brightness());
  });

  touchController.events().onSwipeDown([](TouchPoint s, TouchPoint e) {
    if (display.brightness() == 1) {
      display_brightness(0, false);
    } else {
      display_brightness(max(1, display.brightness() - 10), false);
    }
    Logger::debug("Brightness: %d%% (decrease)", display.brightness());
  });

  touchController.events().onTouch(onTouchLog);
  touchController.events().onHold(onHoldHandler);
  touchController.events().onDblClick(onDblClickHandler);
  touchController.events().onSwipeLeft(onSwipeLeftHandler);
  touchController.events().onSwipeRight(onSwipeRightHandler);
  touchController.events().onSwipeUp(onSwipeUpHandler);
  touchController.events().onSwipeDown(onSwipeDownHandler);
  touchController.events().onSwipeFromBottom(onSwipeFromBottomHandler);
  touchController.events().onSwipeFromTop(onSwipeFromTopHandler);
  touchController.events().onSwipeFromLeft(onSwipeFromLeftHandler);
  touchController.events().onSwipeFromRight(onSwipeFromRightHandler);

  Logger::info("TouchScreen controller done");
#else
  Logger::info("TouchScreen not found (disabled)!");
#endif
}

void setupLittleFS() {
#if defined(ESP8266)
  bool mounted = LittleFS.begin();
#else
  bool mounted = LittleFS.begin(true);
#endif

  if (!mounted) {
    Logger::error("LittleFS mount failed!");
  } else {
    Logger::info("LittleFS mounted successfully (done)");
  }
}

void setupMqttClient() {
  static TLogger _logger{"mqtt"};

  mqtt.begin();

  mqtt.publish(MQTT_LWT_TOPIC, "dummy-init-message");
  scheduler.addCronTask(5 * 60 * 1000UL, []() { mqtt.publish(MQTT_LWT_TOPIC, "hearbeat"); });

  mqtt.addStringListener("mykola-lavryk/#", [](const char* topic, const char* payload) {
    // char t[9] = ""; ntp.ftime("%H:%M:%S", t, sizeof(t));
    _logger.info(">>> %s::%s", topic, payload);
  });

  // LWT_TOPIC "mykola-lavryk:devices/${PIOENV}/status"
  mqtt.addStringListener("mykola-lavryk/devices/+/status", [](const char* topic, const char* payload) {
    char t[9] = ""; ntp.ftime("%H:%M:%S", t, sizeof(t));
    _logger.info("%s %-45.45s LWT:%s", t, topic, payload);
  });

  dispatcher.addListener(EVT_REBOOT, [](IEvent& e) { mqtt.disconnect("reboot"); });

#if LIGHT_SENSOR_PIN > 0
  // publish mqtt
  lightSensor.addListener([]() {
      _logger.debug("mykola-lavryk/devices/" PIO_PIOENV "/light-sensor => %d", lightSensor.value());
      mqtt.publishNumber<int>("mykola-lavryk/devices/" PIO_PIOENV "/light-sensor", (int)lightSensor.value());
  });
  _logger.info("mykola-lavryk/devices/" PIO_PIOENV "/light-sensor MQTT done.");
#else
  // subscribe on mqtt
  mqtt.addNumberListener<int>(
    "mykola-lavryk/devices/+/light-sensor",
    [](const char* topic, int value) {
      char t[9] = ""; ntp.ftime("%H:%M:%S", t, sizeof(t)); 
      _logger.info("%s %-45s val:%d%%", t, topic, value); 
    });
  _logger.info("mykola-lavryk/devices/+/light-sensor listen");
#endif

  commandHandler.registerCommand("dump-mqtt", "show MQTT status", [](const String args) {
    _logger.info("isConnected = %s", mqtt.isConnected() ? "yes" : "no");
  });

  static uint32_t i = 0;
  mqtt.addNumberListener<uint32_t>(
    "mykola-lavryk/int32/#",
    [](const char* t, uint32_t v) {
      char w[9] = ""; ntp.ftime("%H:%M:%S", w, sizeof(w)); 
      _logger.info("%s %-45.45s int:%d", w, t, v); 
    });

  scheduler.addCronTask(1 * 60 * 1000UL,
                        []() { mqtt.publishNumber<uint32_t>("mykola-lavryk/int32/" MQTT_CLIENT_ID, (uint32_t)++i); });

  _logger.info("%s:%d (%s) lwt:%s", MQTT_HOST, MQTT_PORT, MQTT_CLIENT_ID, MQTT_LWT_TOPIC);
}

void setupSD() {
#if BOARD_HAS_SD
#if defined(BOARD_ESP32_S3_LCD147)
  // SD_MMC (4-bit): піни задаються з build_flags (SD_D0/D1/D2/D3/CLK/CMD).
  if (!SD_MMC.setPins(SD_CLK, SD_CMD, SD_D0, SD_D1, SD_D2, SD_D3)) {
    Logger::error("SD_MMC.setPins() fail.");
    return;
  }
 
  const int maxAttempts = 3;
  for (int attempt = 1; attempt <= maxAttempts; ++attempt) {
    if (SD_MMC.begin("/sdcard", /*mode1bit=*/false)) {
      Logger::info("SD_MMC init done (%d/%d)", attempt, maxAttempts);
      return;
    }
    delay(100);
  }
 
  Logger::error("SD_MMC init fail.");
#else
  SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  const int maxAttempts = 3;

  for (int attempt = 1; attempt <= maxAttempts; ++attempt) {
    if (SD.begin(SD_CS, SPI, 4000000)) {
      Logger::info("SD card init done (%d/%d)", attempt, maxAttempts);
      return;
    }
    delay(100);
  }

  Logger::error("SD init fail.");
#endif
#else
  Logger::warn("SD disabled.");
#endif

  return;
}

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
    Logger::info("Free PSRAM:  %d bytes (%.2f Mb)", ESP.getFreePsram() / 1024.0 / 1024.0);
  }
#endif

  Logger::info("");
  Logger::info("WiFi SSID:%s", WiFi.SSID().c_str());
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

void dumpConfigStorage() {
  Logger::info("====== ConfigStorage (NVS) =================================");
  auto entries = configStorage.listEntries();

  if (entries.empty()) {
    Logger::info("(empty.)");
  }

  for (const auto& e : entries) {
    switch (e.type) {
      case NVS_TYPE_U8:
        Logger::info("  key: %-16s type: %-4s value: %s", e.key.c_str(), e.typeName.c_str(),
                     configStorage.getBool(e.key.c_str()) ? "true" : "false");
        break;
      case NVS_TYPE_I8:
      case NVS_TYPE_U16:
      case NVS_TYPE_I16:
      case NVS_TYPE_U32:
      case NVS_TYPE_I32:
      case NVS_TYPE_U64:
      case NVS_TYPE_I64:
        Logger::info("  key: %-16s type: %-4s value: %d", e.key.c_str(), e.typeName.c_str(),
                     configStorage.getInt(e.key.c_str()));
        break;
      case NVS_TYPE_STR:
        Logger::info("  key: %-16s type: %-4s value: %s", e.key.c_str(), e.typeName.c_str(),
                     configStorage.getString(e.key.c_str()).c_str());
        break;
      default:
        Logger::info("  key: %-16s type: %-4s", e.key.c_str(), e.typeName.c_str());
        break;
    }
  }

  Logger::info("");
  Logger::info("Всього записів: %d", entries.size());
  Logger::info("============================================================");
}

#if BOARD_HAS_SD
// ACTIVE_SD — локальний (не глобальний!) макрос-псевдонім лише для двох
// функцій нижче: dumpSDlistDir()/dumpSDInfo(). Визначається безпосередньо
// перед використанням і одразу #undef-иться, щоб не впливати на інший код
// файлу чи транзитивні включення <SD.h> в сторонніх бібліотеках (напр.
// ESP Mail Client -> MB_FS.h), де глобальний "#define SD SD_MMC" ламає
// компіляцію (конфлікт з їхніми власними SD.*-викликами).
#if defined(BOARD_ESP32_S3_LCD147)
#define ACTIVE_SD SD_MMC
#include <SDCardInspector.hpp>
#else
#define ACTIVE_SD SD
#endif
 
void dumpSDlistDir(const char* dirname, uint8_t levels) {
  Logger::info("Вміст директорії: %s", dirname);
 
  File root = ACTIVE_SD.open(dirname);
  if (!root || !root.isDirectory()) {
    Logger::info("  (не вдалось відкрити директорію)");
    return;
  }
 
  File file = root.openNextFile();
  int maxFiles = 50;
  while (file && --maxFiles) {
    if (file.isDirectory()) {
      Logger::info("  DIR : %-30s       ****", file.name());
      if (levels) {
        dumpSDlistDir(file.path(), levels - 1);
      }
    } else {
      Logger::info("  FILE: %-30s SIZE: %u", file.name(), file.size());
    }
    file = root.openNextFile();
  }
  if (file && !maxFiles) {
    Logger::info("  ...");
  }
}
 
void dumpSDInfo() {
  // 1. Деактивируем выбор других устройств на шине
  // digitalWrite(15, HIGH); // Отключаем TFT_CS
  // digitalWrite(33, HIGH); // Отключаем TOUCH_CS
  // digitalWrite(5, HIGH);  // SD_CS = HIGH (пока отключен)
 
  Logger::info("========= SD Card Info =====================================");
 
  uint8_t cardType = ACTIVE_SD.cardType();
 
  if (cardType == CARD_NONE) {
    Logger::info("❌ Картку не вставлено (або тип не визначено).");
    Logger::info("============================================================");
    return;
  }
 
  Logger::info("✅ Картку успішно знайдено!");
 
  // Виводимо тип для деталізації
  if (cardType == CARD_MMC)
    Logger::info("Тип картки: %s", "MMC");
  else if (cardType == CARD_SD)
    Logger::info("Тип картки: %s", "SDSC");
  else if (cardType == CARD_SDHC)
    Logger::info("Тип картки: %s", "SDHC");
  else
    Logger::info("Тип картки: %s", "Невідомий тип");
 
  Logger::info("------------------------------------------------------------");
  dumpSDlistDir("/", 2);
  Logger::info("------------------------------------------------------------");
 
  // Виводимо розмір картки
  uint64_t cardSize = ACTIVE_SD.cardSize() / (1024 * 1024);
  // Serial.printf(F("Розмір картки: %llu MB\n"), cardSize);
  Logger::info("Розмір картки: %s", SizeFormatter::format(ACTIVE_SD.cardSize()));
  Logger::info("Зайнято місця: %s (%.2f%%)", SizeFormatter::format(ACTIVE_SD.usedBytes()),
               ACTIVE_SD.usedBytes() * 100.0 / ACTIVE_SD.cardSize());
  Logger::info("Вільно місця:  %s (%.2f%%)", SizeFormatter::format(ACTIVE_SD.cardSize() - ACTIVE_SD.usedBytes()),
               (ACTIVE_SD.cardSize() - ACTIVE_SD.usedBytes()) * 100.0 / ACTIVE_SD.cardSize());
 
  Logger::info("============================================================");
}
 
#undef ACTIVE_SD
#endif  // BOARD_HAS_SD
 
void dumpLittleFSInfo() {
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
    // SDCardInspector::printAll(SD, logger);
    // SDCardInspector::printAll(SD_MMC, Serial);
    #if defined(BOARD_ESP32_S3_LCD147)
      SDCardInspector::printAll(SD_MMC, logger);
    #else
      SDCardInspector::printAll(SD, logger);
    #endif
  } else if (section.equals("sd+")) {
    dumpSDInfo();
#endif
  } else {
    Logger::warn("Використання: status sys|cfg|sd|sd+|flash|flash+|littlefs");
  }
}

void setupSerialCommander() {
  commandHandler.registerCommand("status", "Показати статус пристрою: status sys|cfg|sd|sd+|flash|flash+|littlefs",
                                 [](const String& args) { dumpStatus(args); });

  commandHandler.registerCommand("reboot", "Перезавантажити пристрій", [](const String& args) {
    dispatcher.dispatch(EVT_REBOOT);
#if defined(BOARD_ESP8266)
    Serial.println("[SystemReset] Rebooting...");
    Serial.flush();
    delay(100);
    ESP.restart();
#else
        SystemReset::reboot();
#endif
  });

  commandHandler.registerCommand("scan", "Сканувати wi-fi мережі", [](const String& args) { WiFi_scan(); });

  commandHandler.registerCommand("flip", "перевернути екран", [](const String& args) { display_flip(); });

  commandHandler.registerCommand("led", "Керування світлодіодом: led on|off", [](const String& args) {
    if (args.equalsIgnoreCase("on")) {
      Logger::info("LED увімкнено");
    } else if (args.equalsIgnoreCase("off")) {
      Logger::info("LED вимкнено");
    } else {
      Logger::info("Використання: led on|off");
    }
  });

  commandHandler.registerCommand("clock", "Керування годинником: clock on|off", [](const String& args) {
    if (args.equalsIgnoreCase("on")) {
      show_clock(true);
    } else if (args.equalsIgnoreCase("off")) {
      show_clock(false);
    } else {
      Logger::info("Керування годинником: clock on|off");
    }
  });

  commandHandler.registerCommand("brightness", "Керування яскравістю: brightness 0-100", [](const String& args) {
    if (args.length() == 0) {
      Logger::info("Використання: brightness 0-100|auto");
    } else if (args.equalsIgnoreCase("auto")) {
#if LIGHT_SENSOR_PIN > 0
      display_brightness(lightSensor.value(), true);
      Logger::info(" isAutoBrighness = %s", isAutoBrightness ? "true" : "false");
#else
      Logger::info(" isAutoBrighness **disabled**");
#endif
    } else {
      display_brightness(args.toInt(), false);
    }
  });

  Logger::info("SerialCommander setup done");
}

void setupBackgroundImage() {
#if defined(LITTLEFS_BACKGROUND_IMAGE)
  spaceImage.loadFromLittleFS(LITTLEFS_BACKGROUND_IMAGE,
                              SPRITE_COLOR_DEPTH > 8 ? JpegColorDepth::RGB565 : JpegColorDepth::RGB332);
  setBackgroundImage(spaceImage);
#endif
}

void setupConfigStorage() {
  configStorage.begin(PIO_PIOENV);
  showClock = configStorage.getBool(CFG_SHOW_CLOCK, true);
  Logger::info("ConfigStorage init done");
}

void loadConfig() {
  showClock = configStorage.getBool(CFG_SHOW_CLOCK, true);
  isAutoBrightness = configStorage.getBool(CFG_SYS_AUTOBRIGHTNESS, false);
  display_brightness(configStorage.getInt(CFG_DISPLAY_BRIGHTNESS, 50), isAutoBrightness);
  Logger::info("ConfigStorage load done");

  Logger::info("\t- %s = %s", CFG_SHOW_CLOCK, showClock ? "ON" : "OFF");
  Logger::info("\t- %s = %s", CFG_SYS_AUTOBRIGHTNESS, isAutoBrightness ? "true" : "false");
  Logger::info("\t- %s = %d", CFG_DISPLAY_BRIGHTNESS, configStorage.getInt(CFG_DISPLAY_BRIGHTNESS, 50));
  Logger::info("");
}

void setupEventDispatcher() { Logger::info("EventDispatcher setup done"); }

void setupTaskCommander() {}

void setupLightSensor() {
#if LIGHT_SENSOR_PIN > 0
  lightSensor.begin();
  scheduler.addCronTask(0, []() { lightSensor.update(); });

  lightSensor.addListener([]() {
    Logger::info("lightSensor.value() = %4d (%3d%%)", lightSensor.read(), lightSensor.value());
    if (isAutoBrightness) {
      display_brightness(lightSensor.value(), isAutoBrightness);
      // Logger::info("display.brightness(%d) *auto*", lightSensor.value());
    }
  });

  /* scheduler.addCronTask(0, []() {
    display.setTextSize(1);
    display.setTextColor(TFT_DARKGREY);
    display.setCursor(10, display.height() - 1 * (5 + display.fontHeight()));
    display.printf("LightSensor: %4d (%3d%%)", lightSensor.read(), lightSensor.value());
  }); */

#if BOARD_HAS_TOUCHSCREEN
  touchController.events().onHold([](TouchPoint p, unsigned long ms) {
    configStorage.setBool(CFG_SYS_AUTOBRIGHTNESS, isAutoBrightness = true);
    display_brightness(lightSensor.value(), isAutoBrightness);
  });

  SwipeCallback onSwipe = [](TouchPoint s, TouchPoint e) {
    configStorage.setBool(CFG_SYS_AUTOBRIGHTNESS, isAutoBrightness = false);
  };

  touchController.events().onSwipeUp(onSwipe);
  touchController.events().onSwipeDown(onSwipe);
#endif
#endif
}

void sendEmail() {
  static bool once = false;
  if (once) {
    return;
  }

  once = true;
  display.drawText(10, 10 + 3 * (3 + display.fontHeight()), "SMTP sendmail", TFT_LIGHTGREY);
  display.flush();
#if HAS_GMAIL_SENDER
// mailer.sendEmail("nick.lavrik@gmail.com", PIO_PIOENV, "hhhh");
#endif
  display.drawText(10, 10 + 4 * (3 + display.fontHeight()), "SMTP sendmail (done)", TFT_LIGHTGREY);
  display.flush();
}

void drawSystemInfo() {
  char buf[120] = "";
  uint8_t row = 0;
  // img.fillRect(0, 30, 320, 65, BG_COLOR);

  uint32_t freeHeap = ESP.getFreeHeap();
  uint32_t cpuFreq = ESP.getCpuFreqMHz();
  uint32_t uptimeSec = millis() / 1000;

  display.setTextSize(1);
  display.setTextFont(1);
  display.setTextColor(TFT_DARKGREY);

#if defined(ESP32)
  display.setCursor(10, 10 + row++ * (5 + display.fontHeight()));
  display.printf(F("Uptime: %02d:%02d:%02d"), uptimeSec / 3600, (uptimeSec / 60) % 60, uptimeSec % 60);
#endif

#if defined(ESP8266)
  display.setCursor(0, 1 + row++ * (2 + display.fontHeight()));
  snprintf(buf, sizeof(buf), "CPU: %dMHz\nLoop rate: %d/s", cpuFreq, display.loopFrameRate());
  display.print(buf);

  enum ScreenMode { DISPLAY_INFO, NETWORK, UPTIME };
  static ScreenMode currentScreen = NETWORK;
  static uint32_t currentScreenTs = millis();
  const uint32_t screenDelayMs = 3 * 1000UL;
  uint32_t hfree; uint32_t hmax; uint8_t hfrag;
  
  switch (currentScreen) {
    case DISPLAY_INFO:
      snprintf(buf, sizeof(buf), "Display: %dx%d\nBrightness: %d\n", TFT_WIDTH, TFT_HEIGHT, display.brightness());
      break;
    case NETWORK:
      snprintf(buf, sizeof(buf), "WiFi: %s\nIP:   %s", WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
      break;
    case UPTIME:
      ESP.getHeapStats(&hfree, &hmax, &hfrag);
      snprintf(buf, sizeof(buf), "Heap: %d / %d KB\nUptime: %02d:%02d:%02d", 
          hmax / 1024, hfree / 1024,
          uptimeSec / 3600, (uptimeSec / 60) % 60, uptimeSec % 60);
      break;
  }

  display.setCursor(0, TFT_HEIGHT - 2 * (0 + display.fontHeight()));
  display.print(buf);

  if (millis() - currentScreenTs > screenDelayMs) {
    currentScreen = (ScreenMode)((currentScreen + 1) % 3);
    currentScreenTs = millis();
  }

#else
  display.setCursor(10, 10 + row++ * (5 + display.fontHeight()));
  snprintf(buf, 120, "CPU: %d MHz   Loop rate: %d/s", cpuFreq, display.loopFrameRate());
  display.print(buf);
#endif


#if defined(ESP8266)
  // ESP8266 не має ESP.getHeapSize() - показуємо лише вільну пам'ять
  // display.setCursor(10, 10 + row++ * (5 + display.fontHeight()));
  // display.printf("Heap free: %d KB", freeHeap / 1024);
#else
  uint32_t totalHeap = ESP.getHeapSize();
  display.setCursor(10, 10 + row++ * (5 + display.fontHeight()));
  display.printf("Heap free: %d KB / %d KB (%d%%)", freeHeap / 1024, totalHeap / 1024, (freeHeap * 100) / totalHeap);
#endif

#if defined(ESP32)
  char* dumpPingStr = dumpPingStatsStr();
  if (dumpPingStr) {
    display.setCursor(10, 10 + row++ * (5 + display.fontHeight()));
    display.print(dumpPingStatsStr());
  }

  snprintf(buf, sizeof(buf), "Brightness: %d%% %s", display.brightness(), isAutoBrightness ? "(auto)" : "");
  display.setCursor(10, 10 + row++ * (5 + display.fontHeight()));
  display.print(buf);

  #if LIGHT_SENSOR_PIN > 0
    // display.setTextSize(1);
    // display.setTextColor(TFT_DARKGREY);
    // display.setCursor(10, display.height() - 1 * (5 + display.fontHeight()));
    display.setCursor(10, 10 + row++ * (5 + display.fontHeight()));
    display.printf("LightSensor: %4d (%3d%%)", lightSensor.read(), lightSensor.value());
  #endif

  #if SCREEN_LOG_TAIL_LINES > 0
  display.setCursor(10, 10 + row++ * (5 + display.fontHeight()));
  display.println("------------------------------------");
  #if BOARD_TTGO_T1 || BOARD_ST7789
  int skip = 11; // hide logvele and tag
  #elif BOARD_ESP32_S3_LCD147
  int skip = 11; // hideloglevel only!
  #else
  int skip = 0;
  #endif

  ScreenLogTail& tail = screenLogTail();
  for (size_t i = tail.count(); i ;--i) {
    display.println(tail.line(i-1) + skip);  // від найстарішого (0) до найновішого
  }
  #endif
#endif

  // Візуальний бар пам'яті
  // int barX = 10, barY = 56, barW = 300, barH = 10;
  // img.drawRect(barX, barY, barW, barH, GRID_COLOR);
  // int fillW = (heapPercent * (barW - 2)) / 100;
  // uint16_t barColor = heapPercent > 30 ? TFT_GREEN : (heapPercent > 15 ? TFT_YELLOW : TFT_RED);
  // img.fillRect(barX + 1, barY + 1, fillW, barH - 2, barColor);

  // int lightPercent = readLightPercent();
  // img.setCursor(180, 70);
  // img.printf("Light: %d%%", lightPercent);
}

void drawTime() {
  static uint32_t lastErrorMs = 0;
  struct tm timeinfo;
  if (!ntp.isSynced()) {
    const char* msg = "sync fail";
    int x, y;
    display.setTextSize(2);
    display.setTextColor(TFT_RED);
    display.setCursor(
      x = max(0, (int) (display.width() - display.textWidth(msg)) / 2),
      y = max(0, (int) (display.height() - display.fontHeight()) / 2)
    );
    // 128x64.108
    // (128-108)/2 = 10
    // Logger::warn("Time sync failed!, pos(%d, %d, %dx%d.%d)", x, y, display.width(), display.height(), display.textWidth(msg));
    if (lastErrorMs == 0) {
      lastErrorMs = millis() - 2000; // first message in 3 sec, all other after 5 second
    }
    if (millis() - lastErrorMs > 5000) {
      Logger::warn("Time sync failed!");
      lastErrorMs = millis();
    }
    display.print(msg);
    display.setTextSize(1);
    return;
  }

  char timeStr[16];
  ntp.ftime("%H:%M:%S", timeStr, sizeof(timeStr));
  // ntp.ftime("%H:%M:%S.%Q", timeStr, sizeof(timeStr));

#if BOARD_TTGO_T1 || BOARD_ESP32_S3_LCD147
  // time
  display.setTextFont(7);  // великий "цифровий" шрифт (тільки цифри та ":")
  // display.setTextSize(1);

  int textW = display.textWidth(timeStr);
  int textH = display.fontHeight();
  int x = (display.width() - textW) / 2;
  int y = 30;

  // Затираємо попередній текст перед виводом нового
  // display.fillRect(0, y, display.width(), textH, TFT_BLACK);

  // display.setTextColor(TFT_DARKGREY);
  display.setTextColor(TFT_CYAN);
  display.setCursor(x, y);
  display.print(timeStr);

  // date
  char dateStr[16];
  ntp.ftime("%d.%m.%Y", dateStr, sizeof(dateStr));

  display.setTextFont(4);
  display.setTextSize(1);

  textW = display.textWidth(dateStr);
  x = (display.width() - textW) / 2;
  y = 93;

  // display.fillRect(0, y, display.width(), display.fontHeight(), TFT_BLACK);

  // display.setTextColor(TFT_DARKGREEN);
  display.setTextColor(TFT_ORANGE);
  display.setCursor(x, y);
  display.print(dateStr);

  display.setTextSize(1);
  display.setTextFont(1);
#elif BOARD_ESP8266
  // display.flip();
  display.setTextSize(2);
  display.setTextColor(SSD1306_WHITE);
  int16_t x1, y1;
  uint16_t textW, textH;

  // display.getTextBounds(timeStr, 0, 0, &x1, &y1, &textW, &textH);
  textW = display.textWidth(timeStr);
  int x = (TFT_WIDTH - textW) / 2;
  display.setCursor(x, 25);
  display.print(timeStr);

  // Менша дата під часом
  ntp.ftime("%d.%m.%Y", timeStr, sizeof(timeStr));

  display.setTextSize(1);
  // display.getTextBounds(dateStr, 0, 0, &x1, &y1, &textW, &textH);
  textW = display.textWidth(timeStr);
  // x = (TFT_WIDTH - textW) / 2;
  display.setCursor(TFT_WIDTH - textW, 0);
  display.print(timeStr);
/* #elif BOARD_4848S040
  int x = 1; int y = 1; int f = 0;
  display.setTextColor(TFT_WHITE);
  display.setTextSize(1);
  ++f; display.setTextFont(f); display.setCursor(x, y); display.printf("%d info %s", f, timeStr); y += 3 + display.fontHeight(); // 1
  display.setTextColor(TFT_GREENYELLOW);
  ++f; display.setTextFont(f); display.setCursor(x, y); display.printf("%d info %s", f, timeStr); y += 3 + display.fontHeight(); // 2
  ++f; // display.setTextFont(f); display.setCursor(x, y); display.printf("%d %s", f, timeStr); y += 3 + display.fontHeight(); // 3
  display.setTextColor(TFT_ORANGE);
  ++f; display.setTextFont(f); display.setCursor(x, y); display.printf("%d info %s", f, timeStr); y += 3 + display.fontHeight(); // 4
  display.setTextColor(TFT_DARKGREY);
  ++f; // display.setTextFont(f); display.setCursor(x, y); display.printf("%d %s", f, timeStr); y += 3 + display.fontHeight(); // 5
  display.setTextColor(TFT_DARKGREEN);
  ++f; display.setTextFont(f); display.setCursor(x, y); display.printf("%d -. %s", f, timeStr); y += 3 + display.fontHeight(); // 6
  display.setTextColor(TFT_CYAN);
  ++f; display.setTextFont(f); display.setCursor(x, y); display.printf("%d +, %s", f, timeStr); y += 3 + display.fontHeight(); // 7
  display.setTextColor(TFT_MAGENTA);
  ++f; display.setTextFont(f); display.setCursor(x, y); display.printf("%d %s", f, timeStr); y += 3 + display.fontHeight(); // 8
  display.setTextFont(1); */
#else
  display.setTextSize(2);
  display.setTextColor(TFT_LIGHTGREY);
  display.setCursor(max(0, display.width() - display.textWidth(timeStr) - 15), 8);
  display.print(timeStr);
#endif
}

void setupFlipButton() {
#if defined(FLIP_BUTTON_PIN)
  // GPIO - INPUT, OUTPUT, INPUT_PULLUP, or INPUT_PULLDOWN
  // - INPUT: Sets the pin as a regular digital read.
  // - OUTPUT: Sets the pin to send out a 3.3V high or 0V low signal.
  // - INPUT_PULLUP: Turns on a built-in resistor holding the pin HIGH until pulled to ground.
  // - INPUT_PULLDOWN: Turns on a built-in resistor holding the pin LOW until supplied with 3.3V.
  pinMode(FLIP_BUTTON_PIN, INPUT_PULLUP);  // GPIO0 - Enable pull-up resistor
  scheduler.addCronTask(0, []() -> void {
    static bool flipButtonPressed = false;
    static uint32_t flippButtonPressedTs = 0;
    static uint8_t _brightness = 0;
    static bool _autoBrightness = false;
    static bool _pause = false;
    uint32_t now = millis();

    int buttonState = digitalRead(FLIP_BUTTON_PIN);
    if ((buttonState == LOW) && !flipButtonPressed) {
      _pause = false;
      flipButtonPressed = true;
      flippButtonPressedTs = millis();
      Logger::info("Button pressed!");
    } else if (buttonState == LOW) {
      // loop (pressed) ....
      if (_pause) {
        // "hide/show" action done!
      } else if (now - flippButtonPressedTs > 3000UL) {
        // "hide/show" action done!
        _pause = true;
        if (display.brightness() == 0) {
          display_brightness(max(_brightness, (uint8_t)1), _autoBrightness);
        } else {
          _brightness = display.brightness();
          _autoBrightness = isAutoBrightness;
          display_brightness(0, false);
        }
      }
    } else if (flipButtonPressed) {
      if (now - flippButtonPressedTs < 1000UL) {
        show_clock(!showClock);
      }
      flipButtonPressed = false;
      flippButtonPressedTs = 0;
      Logger::info("Button released!");
    } else {
      // loop (released) ...
    }
  });
  Logger::info("FlipButton GPIO PIN=%d", FLIP_BUTTON_PIN);
#endif
}

void setupBlinkLED() {
#if BLINK_LED_PIN
  pinMode(BLINK_LED_PIN, OUTPUT);
  TaskId blinkLedTaskId = scheduler.addCronTask(10, []() {
    static char lastMs[9] = "00:00:00";
    char now[9];
    ntp.ftime("%H:%M:%S", now, 9);
    if (strncmp(lastMs, now, sizeof(now))) {
      strncpy(lastMs, now, sizeof(now));
      digitalWrite(BLINK_LED_PIN, LOW);  // увімкнути (інверсна логіка!)
      delay(1);
      digitalWrite(BLINK_LED_PIN, HIGH);  // вимкнути
    }
  });

  if (!configStorage.getBool(CFG_BLINK_LED, true)) {
    scheduler.pause(blinkLedTaskId);
  }

  commandHandler.registerCommand("blink", "Керування LED: blink on|off", [blinkLedTaskId](const String& args) {
    if (args.equalsIgnoreCase("on")) {
      scheduler.resume(blinkLedTaskId);
      configStorage.setBool(CFG_BLINK_LED, true);
      Logger::info("blink ON");
    } else if (args.equalsIgnoreCase("off")) {
      scheduler.pause(blinkLedTaskId);
      configStorage.setBool(CFG_BLINK_LED, false);
      Logger::info("blink OFF");
    } else {
      Logger::info("Керування LED: blink on|off");
    }
  });
#endif
}

#if defined(ESP8266)
#define TFT_WHITE WHITE
#define TFT_GREEN WHITE
#define TFT_DARKGREY WHITE
#define TFT_DARKGREEN WHITE
#endif
#include <MonoIcon16x16.hpp>
MonoIcon16x16 icon;
void setupWiFiIcon() {

  #if defined(BOARD_ESP8266)
    const int p[2] = {display.width() - 16, display.height() - 16};
  #else
    const int p[2] = {display.width() - 16, 0};
  #endif

  Logger::info("================ Display %dx%d", display.width(), display.height());

  scheduler.addCronTask(0, [p]() {
    /* display.drawRect(0, 0, 2, 2, TFT_GREEN);
    display.drawRect(10, 10, 2, 2, TFT_GREEN);
    display.drawRect(20, 20, 2, 2, TFT_GREEN);
    display.drawRect(display.width()-2, 0, 2, 2, TFT_GREEN);

    display.drawRect(TFT_WIDTH - 16, TFT_HEIGHT - 16, 2, 2, TFT_WHITE);
    display.drawRect(TFT_WIDTH - 20, TFT_HEIGHT - 20, 12, 12, TFT_GREEN); */


    if (WiFi.isConnected()) {
      display.drawBitmap(p[0], p[1],
        (const uint8_t*) icon.wifi().data(),
        16, 16, TFT_DARKGREEN);
    } else {
      display.drawBitmap(p[0], p[1], 
        (const uint8_t*) ((uint)(millis() % 1000) >= 450 ? icon.wifi().data() : icon.empty().data()),
        16, 16, TFT_DARKGREY);
    }
  });
}

void testAsusWRT() {
  Logger::info("====== AsusWRT test script =======");
  Logger::info("free heap: %u", ESP.getFreeHeap());

  const char* path = "/asus-get_clientlist.json";
  File file = LittleFS.open(path, "r");
  if (!file || file.isDirectory()) {
    Logger::error("Can't open file (%s)", path);
    return;
  }

  Logger::info("free heap before read: %u", ESP.getFreeHeap());
  String json = file.readString();
  file.close();
  Logger::info("free heap after read (json size=%u): %u", json.length(), ESP.getFreeHeap());

  std::vector<RouterClientInfo> clients;
  if (!RouterClientListParser::parse(json, clients)) {
    Logger::error("can't parse client list json. [%d]", clients.capacity());
  }

  Logger::info("free heap after parse: %u", ESP.getFreeHeap());

  RouterClientListIterator it(std::move(clients));
  while (it.hasNext()) {
    const RouterClientInfo& c = it.next();
    Logger::info("client.name=%s", c.name.c_str());
  }
  Logger::info("====== AsusWRT test script =======");
  Logger::info("");
}

void setup() {
  uint32_t freeHeap = ESP.getFreeHeap();
  int i = 1;
  setupSerial();
  Logger::info("free heap before setup: %u", freeHeap);
  Logger::info("free heap after SetupSerial: %u", ESP.getFreeHeap());
  setupSD();
  Logger::info("free heap after SetupSD: %u", ESP.getFreeHeap());
  setupLittleFS();
  Logger::info("free heap step#%d: %u", i++, ESP.getFreeHeap()); // 1
  setupEventDispatcher();
  Logger::info("free heap step#%d: %u", i++, ESP.getFreeHeap()); // 2
  setupConfigStorage();
  Logger::info("free heap step#%d: %u", i++, ESP.getFreeHeap()); // 3
  setupSerialCommander();
  Logger::info("free heap step#%d: %u", i++, ESP.getFreeHeap()); // 4
  setupBlinkLED();
  Logger::info("free heap step#%d: %u", i++, ESP.getFreeHeap()); // 5
  setupDisplay();
  Logger::info("free heap step#%d: %u", i++, ESP.getFreeHeap()); // 6
  setupTouchScreen();
  Logger::info("free heap step#%d: %u", i++, ESP.getFreeHeap()); // 7
  setupWiFi();
  Logger::info("free heap step#%d: %u", i++, ESP.getFreeHeap()); // 8
  setupNtpService();
  Logger::info("free heap step#%d: %u", i++, ESP.getFreeHeap()); // 9
  setupBackgroundImage();
  Logger::info("free heap step#%d: %u", i++, ESP.getFreeHeap()); // 10
  setupTaskCommander();
  Logger::info("free heap step#%d: %u", i++, ESP.getFreeHeap()); // 11
  setupLightSensor();
  Logger::info("free heap step#%d: %u", i++, ESP.getFreeHeap()); // 12
  setupMqttClient();
  Logger::info("free heap step#%d: %u", i++, ESP.getFreeHeap()); // 13
  setupFlipButton();
  Logger::info("free heap step#%d: %u", i++, ESP.getFreeHeap()); // 14
  setupWiFiIcon();
  Logger::info("free heap step#%d: %u", i++, ESP.getFreeHeap()); // 15
  loadConfig();
  Logger::info("free heap step#%d: %u", i++, ESP.getFreeHeap()); // 16

  httpServer.setStaticSource(&littleFsSource);
  Logger::info("free heap step#%d: %u", i++, ESP.getFreeHeap()); // 17
  // httpServer.setEventDispatcher(&dispatcher);
  httpServer.begin();
  Logger::info("free heap step#%d: %u", i++, ESP.getFreeHeap()); // 18
  Logger::info("HttpServer::begin()");
  Logger::info("LittlFS::exists('/convert.c') = %s", littleFsSource.exists("/convert.c") ? "yes" : "no");
  Logger::info("LittleFS test done.");

  display.flush();
  testAsusWRT();
  Logger::info("> Ready. Введіть 'list' для перегляду команд.");
}

void loop() {
  display.startWrite();
  PrintQueue::flush();
  doPing();
  drawBackgroundImage();
  drawSystemInfo();

  mqtt.loop();
  commandHandler.update();

  if (showClock) drawTime();

  // sendEmail();

  scheduler.loop();

  display.endWrite();

#if BOARD_HAS_TOUCHSCREEN
  touchController.update();
#endif

  display.flush();

  delay(16);
}
