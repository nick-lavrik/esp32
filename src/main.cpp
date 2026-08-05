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
#include <SD.h>

#include <SDCardInspector.hpp>
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

#include "BackgroundImages.hpp"
#include "SizeFormatter.hpp"
#include "ntp.h"
#include "ping.h"
#include "setup.h"
#include "wifi.h"

#if BOARD_HAS_TOUCH
#include <TouchController.h>
#endif

const char* EVT_REBOOT = "reboot";
const char* CFG_SHOW_CLOCK = "clock";
const char* CFG_BLINK_LED = "blink"; // ESP8266 BLINK_LED_PIN dependency
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

#if BOARD_HAS_DISPLAY
Display display;
TouchScreenConfig displayConfig = makeTouchScreenConfig();
#endif

#if BOARD_HAS_TOUCH
TouchPointMapper mapper(displayConfig);
TouchEvents touch(displayConfig);
TouchController touchController;
#endif

#if HAS_GMAIL_SENDER
GmailSender mailer(GMAIL_EMAIL, GMAIL_PASSWORD, "ESP32 Device");
#endif

#if LIGHT_SENSOR_PIN > 0
AnalogSensor lightSensor(LIGHT_SENSOR_PIN, 0, 1855, 100, 0, 3);
#endif

#if BOARD_HAS_TOUCH
void onTouchLog(TouchPoint p) { Logger::debug("Touch: %d, %d", p.x, p.y); }
void onHoldHandler(TouchPoint p, unsigned long ms) {
  Logger::debug("Hold at %d,%d for %lu ms", p.x, p.y, ms);
}
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
void onSwipeFromLeftHandler(TouchPoint start, TouchPoint end) {
  Logger::debug("Swipe FROM LEFT (напр., назад)");
}
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

void display_flip() {
  displayConfig.invertY = !displayConfig.invertY;
  displayConfig.invertX = !displayConfig.invertX;
  display.flip();
}

void setupTouchScreen() {
#if BOARD_HAS_TOUCH
  touch.setTouchPointMapper(&mapper);
  touchController.setup(&touch);
  Logger::debug("TouchScreen setup done");

  touchController.events().onHold(onHoldDrawPoints);

  touchController.events().onSwipeUp([](TouchPoint s, TouchPoint e) {
    if (display.brightness() == 0) {
      display.brightness(1);
    } else if (display.brightness() == 1) {
      display.brightness(10);
    } else {
      display.brightness(min(100, display.brightness() + 10));
    }
    Logger::debug("Brightness: %d%% (increase)", display.brightness());
  });

  touchController.events().onSwipeDown([](TouchPoint s, TouchPoint e) {
    if (display.brightness() == 1) {
      display.brightness(0);
    } else {
      display.brightness(max(1, display.brightness() - 10));
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

  // LWT_TOPIC "mykola-lavryk:devices/${PIOENV}/status"
  mqtt.addStringListener("mykola-lavryk/devices/+/status",
                         [](const char* topic, const char* payload) -> bool {
                           _logger.info("topic:%s payload:%s", topic, payload);
                           return true;
                         });

  dispatcher.addListener(EVT_REBOOT, [](IEvent& e) { mqtt.disconnect("reboot"); });

#if LIGHT_SENSOR_PIN > 0
/* lightSensor.addListener([]() {
    // mqtt.publishStruct("mykola-lavryk:sensors/ldr", )
    // isAutoBrightness, lightSensor.read(), lightSensor.value())
}); */
#else
// subscribe on mqtt
#endif

  scheduler.addCronTask(5 * 60 * 1000UL, []() { mqtt.publish(MQTT_LWT_TOPIC, "hearbeat"); });
  commandHandler.registerCommand("dump-mqtt", "show MQTT status", [](const String args) {
    _logger.info("isConnected = %s", mqtt.isConnected() ? "yes" : "no");
  });

  static uint32_t i = 0;
  mqtt.addNumberListener<uint32_t>("mykola-lavrik/int32/#", [](const char* t, uint32_t v) {
    _logger.info("topic:%s int32:%d", t, v);
  });
  scheduler.addCronTask(1 * 60 * 1000UL, []() {
    mqtt.publishNumber<uint32_t>("mykola-lavrik/int32/" MQTT_CLIENT_ID, (uint32_t)++i);
  });

  _logger.info("%s:%d (%s) ...", MQTT_HOST, MQTT_PORT, MQTT_CLIENT_ID);
}

void setupSD() {
#if BOARD_HAS_SD
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
  Logger::info("Flash size:  %d bytes (%.2f MB)", ESP.getFlashChipSize(),
               ESP.getFlashChipSize() / 1024.0 / 1024.0);
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
    Logger::info("Total PSRAM: %d bytes (%.2f Mb)", ESP.getPsramSize(),
                 ESP.getPsramSize() / 1024.0 / 1024.0);
    Logger::info("Free PSRAM:  %d bytes (%.2f Mb)", ESP.getFreePsram() / 1024.0 / 1024.0);
  }
#endif

  Logger::info("");
  Logger::info("WiFi SSID:%s", WiFi.SSID().c_str());
  if (WiFi.status() == WL_CONNECTED) {
    Logger::info("WiFi   IP: %s", WiFi.localIP().toString());
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
void dumpSDlistDir(const char* dirname, uint8_t levels) {
  Logger::info("Вміст директорії: %s", dirname);

  File root = SD.open(dirname);
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

  uint8_t cardType = SD.cardType();

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
  uint64_t cardSize = SD.cardSize() / (1024 * 1024);
  // Serial.printf(F("Розмір картки: %llu MB\n"), cardSize);
  Logger::info("Розмір картки: %s", SizeFormatter::format(SD.cardSize()));
  Logger::info("Зайнято місця: %s (%.2f%%)", SizeFormatter::format(SD.usedBytes()),
               SD.usedBytes() * 100.0 / SD.cardSize());
  Logger::info("Вільно місця:  %s (%.2f%%)", SizeFormatter::format(SD.cardSize() - SD.usedBytes()),
               (SD.cardSize() - SD.usedBytes()) * 100.0 / SD.cardSize());

  Logger::info("============================================================");
}
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
    Logger::info("File: %-28s %8d bytes (%s)", file.name(), file.size(),
                 SizeFormatter::format(file.size()).c_str());
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
  Logger::info("Used: %d / Total: %d / Free: %d bytes | Free: %.3f%%", usedBytes, totalBytes,
               totalBytes - usedBytes, freePercent);
  Logger::info("============================================================");
}

void dumpStatus(const String& section) {
  if (section.equals("sys")) {
    dumpSystemInfo();
  } else if (section.equals("cfg")) {
    dumpConfigStorage();
  } else if (section.equals("littlefs")) {
    dumpLittleFSInfo();
  } else if (section.equals("flash")) {
    EspPartitionInspector::printAll(Serial);
  } else if (section.equals("flash+")) {
    EspPartitionInspector::printAll(Serial, true);
#if BOARD_HAS_SD
  } else if (section.equals("sd")) {
    SDCardInspector::printAll(SD, Serial);
    // SDCardInspector::printAll(SD_MMC, Serial);
  } else if (section.equals("sd+")) {
    dumpSDInfo();
#endif
  } else {
    Logger::warn("Використання: status sys|cfg|sd|sd+|flash|flash+|littlefs");
  }
}

void setupSerialCommander() {
  commandHandler.registerCommand(
      "status", "Показати статус пристрою: status sys|cfg|sd|sd+|flash|flash+|littlefs",
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

  commandHandler.registerCommand("scan", "Сканувати wi-fi мережі",
                                 [](const String& args) { WiFi_scan(); });

  commandHandler.registerCommand("flip", "перевернути екран",
                                 [](const String& args) { display_flip(); });

  commandHandler.registerCommand("led", "Керування світлодіодом: led on|off",
                                 [](const String& args) {
                                   if (args.equalsIgnoreCase("on")) {
                                     Logger::info("LED увімкнено");
                                   } else if (args.equalsIgnoreCase("off")) {
                                     Logger::info("LED вимкнено");
                                   } else {
                                     Logger::info("Використання: led on|off");
                                   }
                                 });

  commandHandler.registerCommand("clock", "Керування годинником: clock on|off",
                                 [](const String& args) {
                                   if (args.equalsIgnoreCase("on")) {
                                     configStorage.setBool(CFG_SHOW_CLOCK, showClock = true);
                                     Logger::info("clock ON");
                                   } else if (args.equalsIgnoreCase("off")) {
                                     configStorage.setBool(CFG_SHOW_CLOCK, showClock = false);
                                     Logger::info("clock OFF");
                                   } else {
                                     Logger::info("Керування годинником: clock on|off");
                                   }
                                 });

  commandHandler.registerCommand(
      "brightness", "Керування яскравістю: brightness 0-100", [](const String& args) {
        if (args.length() == 0) {
          Logger::info("Використання: brightness 0-100|auto");
        } else if (args.equalsIgnoreCase("auto")) {
#if LIGHT_SENSOR_PIN > 0
          display.brightness(lightSensor.value());
#if !defined(BOARD_ESP8266)
          configStorage.setBool(CFG_SYS_AUTOBRIGHTNESS, isAutoBrightness = true);
          configStorage.setInt(CFG_DISPLAY_BRIGHTNESS, display.brightness());
#else
            isAutoBrightness = true;
#endif
          Logger::info(" isAutoBrighness = %s", isAutoBrightness ? "true" : "false");
#else
            Logger::info(" isAutoBrighness **disabled**");
#endif
        } else {
          display.brightness(args.toInt());
          configStorage.setInt(CFG_DISPLAY_BRIGHTNESS, display.brightness());
          configStorage.setBool(CFG_SYS_AUTOBRIGHTNESS, isAutoBrightness = false);
          Logger::info("display.brightness(%d)", display.brightness());
        }
      });

  Logger::info("SerialCommander setup done");
}

void setupBackgroundImage() {
#if defined(LITTLEFS_BACKGROUND_IMAGE)
  spaceImage.loadFromLittleFS(LITTLEFS_BACKGROUND_IMAGE, SPRITE_COLOR_DEPTH > 8
                                                             ? JpegColorDepth::RGB565
                                                             : JpegColorDepth::RGB332);
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
  display.brightness(configStorage.getInt(CFG_DISPLAY_BRIGHTNESS, 50));
  Logger::info("ConfigStorage load done");

  Logger::info("\t- %s = %s", CFG_SHOW_CLOCK, showClock ? "ON" : "OFF");
  Logger::info("\t- %s = %s", CFG_SYS_AUTOBRIGHTNESS, isAutoBrightness ? "true" : "false");
  Logger::info("\t- %s = %d", CFG_DISPLAY_BRIGHTNESS,
               configStorage.getInt(CFG_DISPLAY_BRIGHTNESS, 50));
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
      display.brightness(lightSensor.value());
      Logger::info("display.brightness(%d) *auto*", lightSensor.value());
    }
  });

  scheduler.addCronTask(0, []() {
    display.setTextSize(1);
    display.setTextColor(TFT_DARKGREY);
    display.setCursor(10, display.height() - 1 * (5 + display.fontHeight()));
    display.printf("LightSensor: %4d (%3d%%)", lightSensor.read(), lightSensor.value());
  });

#if BOARD_HAS_TOUCH
  touchController.events().onHold([](TouchPoint p, unsigned long ms) {
    configStorage.setBool(CFG_SYS_AUTOBRIGHTNESS, isAutoBrightness = true);
    configStorage.setInt(CFG_DISPLAY_BRIGHTNESS, lightSensor.value());
    display.brightness(lightSensor.value());
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
  uint8_t row = 0;
  // img.fillRect(0, 30, 320, 65, BG_COLOR);

  uint32_t freeHeap = ESP.getFreeHeap();
  uint32_t cpuFreq = ESP.getCpuFreqMHz();
  uint32_t uptimeSec = millis() / 1000;

  display.setTextSize(1);
  display.setTextColor(TFT_DARKGREY);

  display.setCursor(10, 10 + row++ * (5 + display.fontHeight()));
  display.printf(F("Uptime: %02d:%02d:%02d"), uptimeSec / 3600, (uptimeSec / 60) % 60,
                 uptimeSec % 60);

  display.setCursor(10, 10 + row++ * (5 + display.fontHeight()));
  display.printf(F("CPU: %d MHz   Loop rate: %d/s"), cpuFreq, display.loopFrameRate());

#if defined(BOARD_ESP8266)
  // ESP8266 не має ESP.getHeapSize() - показуємо лише вільну пам'ять
  display.setCursor(10, 10 + row++ * (5 + display.fontHeight()));
  display.printf("Heap free: %d KB", freeHeap / 1024);
#else
  uint32_t totalHeap = ESP.getHeapSize();
  display.setCursor(10, 10 + row++ * (5 + display.fontHeight()));
  display.printf("Heap free: %d KB / %d KB (%d%%)", freeHeap / 1024, totalHeap / 1024,
                 (freeHeap * 100) / totalHeap);
#endif

  char* dumpPingStr = dumpPingStatsStr();
  if (dumpPingStr) {
    display.setCursor(10, 10 + row++ * (5 + display.fontHeight()));
    display.print(dumpPingStatsStr());
  }

  char brightnessStr[200];
  sprintf(brightnessStr, "Brigtness: %d%% %s", display.brightness(),
          isAutoBrightness ? "(auto)" : "");
  display.setCursor(10, 10 + row++ * (5 + display.fontHeight()));
  display.print(brightnessStr);

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

void setupFlipButton() {
#if defined(FLIP_BUTTON_PIN)
  // GPIO - INPUT, OUTPUT, INPUT_PULLUP, or INPUT_PULLDOWN
  // - INPUT: Sets the pin as a regular digital read.
  // - OUTPUT: Sets the pin to send out a 3.3V high or 0V low signal.
  // - INPUT_PULLUP: Turns on a built-in resistor holding the pin HIGH until pulled to ground.
  // - INPUT_PULLDOWN: Turns on a built-in resistor holding the pin LOW until supplied with 3.3V.
  pinMode(FLIP_BUTTON_PIN, INPUT_PULLUP);  // GPIO0 - Enable pull-up resistor
  scheduler.addCronTask(0, []() -> void {
    static bool bootButtonPressed = false;
    int buttonState = digitalRead(FLIP_BUTTON_PIN);
    if ((buttonState == LOW) && !bootButtonPressed) {
      bootButtonPressed = true;
      display_flip();
      Logger::info("Button pressed!");
    } else if (buttonState == LOW) {
      // loop (pressed) ....
    } else if (bootButtonPressed) {
      // button release
      bootButtonPressed = false;
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
    Logger::info("BlinkLED onPause!!");
    scheduler.pause(blinkLedTaskId);
  }

  commandHandler.registerCommand(
    "blink", "Керування LED: blink on|off",
    [blinkLedTaskId](const String& args) {
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
    }
  );
#endif
}

void setup() {
  setupSerial();
  setupSD();
  setupLittleFS();
  setupEventDispatcher();
  setupConfigStorage();
  setupSerialCommander();
  setupBlinkLED();
  setupDisplay();
  setupTouchScreen();

  setupWiFi();
  setupNtpService();
  setupBackgroundImage();
  setupTaskCommander();
  setupLightSensor();
  setupMqttClient();
  setupFlipButton();
  loadConfig();

  httpServer.setStaticSource(&littleFsSource);
  // httpServer.setEventDispatcher(&dispatcher);
  httpServer.begin();
  Logger::info("HttpServer::begin()");
  Logger::info("LittlFS::exists('/convert.c') = %s",
               littleFsSource.exists("/convert.c") ? "yes" : "no");
  Logger::info("LittleFS test done.");

  display.flush();
  Logger::info("> Ready. Введіть 'list' для перегляду команд.");
}

void loop() {
  PrintQueue::flush();
  commandHandler.update();
  mqtt.loop();

  // display.startWrite();
  doPing();
  display.clear();
  drawBackgroundImage();
  drawSystemInfo();
  if (showClock) drawTime();

  // sendEmail();

  scheduler.loop();
#if BOARD_HAS_TOUCH
  touchController.update();
#endif

  // display.endWrite();
  display.flush();

  delay(1);
}
