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
#include "Display.h"
#include <SPI.h>
#include <SD.h>
#include <LittleFS.h>
#include <GmailSender.hpp>
#include <ConfigStorage.hpp>
#include <EventDispatcher.hpp>
#include <SerialCommander.hpp>
#include <JpegImage.hpp>
#include <SystemReset.hpp>
#include <EspPartitionInspector.hpp>
#include <SDCardInspector.hpp>
#include "wifi.h"
#include "ntp.h"
#include "ping.h"
#include "BackgroundImages.hpp"
#include "SizeFormatter.hpp"
#include "TouchScreen/TouchController.h"
#include <TaskController.hpp>
#include <PubSubClient.h>
#include <AnalogSensor.hpp>
#include <MqttClient.hpp>

#if defined(SD_SCK) && defined(SD_MISO) && defined(SD_MOSI) && defined(SD_CS) && SD_CS > 0
constexpr bool kHasSD = true;
#else
constexpr bool kHasSD = false;
#endif

const char* EVT_REBOOT PROGMEM = "reboot";
const char* CFG_SYS_AUTOBRIGHTNESS PROGMEM = "auto-brightness";
const char* CFG_DISPLAY_BRIGHTNESS PROGMEM = "brightness";

void setupSerial() {
    Serial.begin(115200);
    delay(200);
    Serial.printf("\n\n-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*\n");
    Serial.printf(" %s (%s)\n", ESP.getChipModel(), PIO_PIOENV );
    Serial.printf("-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*\n\n");
}

void setupDisplay() {
    #if defined(BOARD_ST7789)
    pinMode(TFT_BL, OUTPUT); // st7789
    #endif

    display.init();
    //display.autobrightness(true);
    Serial.println("Display setup done.");
}

TouchScreenConfig makeTouchScreenConfig() {
    TouchScreenConfig c;
    // Приклад: контролер видає сирі 0..4095, екран фізично 320x240,
    // а сама панель ще й повернута (типова ситуація для дешевих SPI TFT).
    // c.rawMinX = 200;  c.rawMaxX = 3900; // підбирається калібруванням
    // c.rawMinY = 200;  c.rawMaxY = 3900;

    #ifdef BOARD_ST7789
    c.rawMinX = 212; c.rawMaxX = 3714;
    c.rawMinY = 329; c.rawMaxY = 3817;

    c.screenWidth  = 320;
    c.screenHeight = 240;

    c.invertY = true;   // якщо вертикаль перевернута
    c.invertX = true;   // якщо горизонталь перевернута
    c.swapXY = false;  // якщо екран повернутий на 90/270 градусів

    c.edgeZoneX = 25;
    c.edgeZoneY = 25;
    #endif

    #ifdef BOARD_4848S040
    c.rawMinX = 0; c.rawMaxX = 480;
    c.rawMinY = 0; c.rawMaxY = 480;

    c.screenWidth  = 480;
    c.screenHeight = 480;

    c.invertX = false;
    c.invertY = true;
    c.swapXY = true;

    c.edgeZoneX = 40;
    c.edgeZoneY = 40;
    #endif

    return c;
}
MqttConfig makeMqttConfig() {
    MqttConfig config;
    config.host = MQTT_HOST,
    config.port = MQTT_PORT,
    config.clientId = MQTT_CLIENT_ID;
    config.username = MQTT_USERNAME;
    config.password = MQTT_PASSWORD;
    config.lwtTopic = MQTT_LWT_TOPIC;
    config.lwtOfflineMessage = MQTT_LWT_MSG_OFFLINE;
    config.lwtOnlineMessage = MQTT_LWT_MSG_ONLINE;

    return config;
}

bool isAutoBrightness = false;
EventDispatcher dispatcher;
Display display;
TaskController scheduler;
TouchController touchController;
TouchScreenConfig touchScreenConfig = makeTouchScreenConfig();
TouchPointMapper mapper(touchScreenConfig);
TouchEvents touch(touchScreenConfig);
ConfigStorage configStorage;
JpegImage spaceImage;
SerialCommander commandHandler;
WiFiClient espClient;
PubSubClient client(espClient);
MqttClient mqtt(makeMqttConfig());

#if HAS_GMAIL_SENDER
GmailSender mailer(GMAIL_EMAIL, GMAIL_PASSWORD, "ESP32 Device");
#endif

#if LIGHT_SENSOR_PIN > 0
AnalogSensor lightSensor(LIGHT_SENSOR_PIN, 0, 1855, 100, 0, 3);
#endif


void onTouchLog(TouchPoint p)                              { Serial.printf("Touch: %d, %d\n", p.x, p.y); }
void onHoldHandler(TouchPoint p, unsigned long ms)         { Serial.printf("Hold at %d,%d for %lu ms\n", p.x, p.y, ms); }
void onDblClickHandler(TouchPoint p)                       { Serial.printf("Double click: %d, %d\n", p.x, p.y); }

void onSwipeLeftHandler(TouchPoint start, TouchPoint end)  { Serial.println("Swipe LEFT"); }
void onSwipeRightHandler(TouchPoint start, TouchPoint end) { Serial.println("Swipe RIGHT"); }
void onSwipeUpHandler(TouchPoint start, TouchPoint end)    { Serial.println("Swipe UP"); }
void onSwipeDownHandler(TouchPoint start, TouchPoint end)  { Serial.println("Swipe DOWN"); }

void onSwipeFromBottomHandler(TouchPoint start, TouchPoint end) { Serial.println("Swipe FROM BOTTOM (напр., відкрити меню)"); }
void onSwipeFromTopHandler(TouchPoint start, TouchPoint end)    { Serial.println("Swipe FROM TOP (напр., шторка сповіщень)"); }
void onSwipeFromLeftHandler(TouchPoint start, TouchPoint end)   { Serial.println("Swipe FROM LEFT (напр., назад)"); }
void onSwipeFromRightHandler(TouchPoint start, TouchPoint end)  { Serial.println("Swipe FROM RIGHT (напр., бокова панель)"); }

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
            display.drawRect(display.width()-1, 0, 1, 1, TFT_WHITE);
            display.drawRect(display.width()-1, display.height() - 1, 1, 1, TFT_WHITE);
            display.drawRect(0, display.height() - 1, 1, 1, TFT_WHITE);

            display.drawRect(
                touchScreenConfig.edgeZoneX, touchScreenConfig.edgeZoneY,
                touchScreenConfig.screenWidth - 2 * touchScreenConfig.edgeZoneX,
                touchScreenConfig.screenHeight - 2 * touchScreenConfig.edgeZoneY,
                TFT_DARKGREY
            );
        },
        1 // з інтервалом 1 мілісекунда, а не на кожному tick()
    );

    Serial.printf("\nONHOLD FRAME !!!\n\n");
}

void display_flip() {
    touchScreenConfig.invertY = !touchScreenConfig.invertY;
    touchScreenConfig.invertX = !touchScreenConfig.invertX;
    display.flip();
}

void setupTouchScreen() {
    touch.setTouchPointMapper(&mapper);
    touchController.setup(&touch);
    Serial.println("TouchScreen setup done");

    touchController.events().onHold(onHoldDrawPoints);

    touchController.events().onSwipeUp([](TouchPoint s, TouchPoint e) {
        if (display.brightness() == 0) {
            display.brightness(1);
        } else if (display.brightness() == 1) {
            display.brightness(10);
        } else {
            display.brightness(min(100, display.brightness() + 10)); 
        }
        Serial.printf(F("Brightness: %d%% (increase)\n"), display.brightness());
    });

    touchController.events().onSwipeDown([](TouchPoint s, TouchPoint e) { 
        if (display.brightness() == 1) {
            display.brightness(0);
        } else {
            display.brightness(max(1, display.brightness() - 10));
        }
        Serial.printf(F("Brightness: %d%% (decrease)\n"), display.brightness());
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

    Serial.println("TouchScreen controller done");
}

void setupLittleFS() {
  if (!LittleFS.begin(true)) {
  // if (!LittleFS.begin(true, "/littlefs", 10, "littlefs")) {
    Serial.println("LittleFS mount failed!");
  } else {
    Serial.println("LittleFS mounted successfully (done)");
  }
}

void setupMqttClient() {
    mqtt.begin();

    mqtt.publish(MQTT_LWT_TOPIC, "156");

    // LWT_TOPIC "mykola-lavryk:devices/${PIOENV}/status"
    mqtt.addStringListener("mykola-lavryk:devices/+/status", [](const char* topic, const char* payload) -> bool {
        Serial.printf("[MQTT] topic:%s payload:%s\n", topic, payload);
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
        Serial.printf("[MQTT] isConnected = %s\n", mqtt.isConnected() ? "yes" : "no");
        Serial.println();
    });

    static uint32_t i = 0;
    mqtt.addNumberListener<uint32_t>("mykola-lavrik:int32/#", [](const char* t, uint32_t v) { Serial.printf("[MQTT] %s int32=%d\n", t, v); });
    scheduler.addCronTask(1 * 60 * 1000UL, []() { mqtt.publishNumber<uint32_t>("mykola-lavrik:int32/" MQTT_CLIENT_ID, (uint32_t)++i); });

    Serial.printf("[MQTT] %s:%d (%s)\n", MQTT_HOST, MQTT_PORT, MQTT_CLIENT_ID);
}

void setupSD() {
  if (!kHasSD) { return; }
  SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  const int maxAttempts = 3;

  for (int attempt = 1; attempt <= maxAttempts; ++attempt) {
    if (SD.begin(SD_CS, SPI, 4000000)) {
      Serial.printf(F("SD card init done (%d/%d)\n"), attempt, maxAttempts);
      return;
    }
    delay(100);
  }

  Serial.println(F("SD init fail."));
  return;
}

void dumpSystemInfo() {
    Serial.println("\n======== ESP32 CHIP INFO ==================================");

    // --- PlatformIO environment ---
    Serial.printf("PlatformIO: %s\n", PIO_PIOENV);

    // --- Модель чипа ---
    Serial.printf("Chip model: %s\n", ESP.getChipModel());
    Serial.printf("Chip revision: %d\n", ESP.getChipRevision());
    Serial.printf("CPU cores: %d\n", ESP.getChipCores());
    Serial.printf("CPU freq: %d MHz\n", ESP.getCpuFreqMHz());

    //  возвращает общее количество тактов процессора (CPU cycles), прошедших с момента запуска
    // Serial.printf("Cycle Count: %d\n", ESP.getCycleCount());

    // --- ESP-IDF ---
    Serial.printf("SDK version:  %s\n", ESP.getSdkVersion());
    Serial.printf("Core version: %s\n", ESP.getCoreVersion());

    // --- Flash ---
    Serial.printf("Flash size:  %d bytes (%.2f MB)\n", ESP.getFlashChipSize(), ESP.getFlashChipSize() / 1024.0 / 1024.0);
    Serial.printf("Flash speed: %d Hz\n", ESP.getFlashChipSpeed());

    // --- Внутрішня RAM (SRAM) ---
    Serial.printf("Total heap:  %d bytes\n", ESP.getHeapSize());
    Serial.printf("Free heap:   %d bytes\n", ESP.getFreeHeap());

    // --- PSRAM ---
    Serial.printf("PSRAM found: %s\n", psramFound() ? "YES" : "NO");
    if (psramFound()) {
        Serial.printf("Total PSRAM: %d bytes (%.2f Mb)\n", ESP.getPsramSize(), ESP.getPsramSize() / 1024.0 / 1024.0);
        Serial.printf("Free PSRAM:  %d bytes (%.2f Mb)\n", ESP.getFreePsram() / 1024.0 / 1024.0);
    }

    Serial.println();
    // Serial.printf("WiFi: %s", WiFi.SSID);
    Serial.printf("Last reset reason: %s\n", SystemReset::getLastResetReason());
    Serial.printf("display.brightness = %d\n", display.brightness());
    /* Serial.println("\n======= ESP32 HEAP INFO ========");
    heap_caps_print_heap_info(MALLOC_CAP_DEFAULT); // друкує все одразу у форматованому вигляді */
    Serial.println("============================================================\n");
}

void dumpConfigStorage() {
    Serial.println("\n====== ConfigStorage (NVS) =================================");
    auto entries = configStorage.listEntries();
 
    if (entries.empty()) {
        Serial.println(F("(empty.)"));
    }
 
    for (const auto& e : entries) {
        switch (e.type) {
            case NVS_TYPE_U8:
                Serial.printf("  key: %-16s type: %-4s value: %s\n", e.key.c_str(), e.typeName.c_str(), configStorage.getBool(e.key.c_str()) ? "true" : "false");
                break;
            case NVS_TYPE_I8:
            case NVS_TYPE_U16:
            case NVS_TYPE_I16:
            case NVS_TYPE_U32:
            case NVS_TYPE_I32:
            case NVS_TYPE_U64:
            case NVS_TYPE_I64:
                Serial.printf("  key: %-16s type: %-4s value: %d\n", e.key.c_str(), e.typeName.c_str(), configStorage.getInt(e.key.c_str()));
                break;
            case NVS_TYPE_STR:
                Serial.printf("  key: %-16s type: %-4s value: %s\n", e.key.c_str(), e.typeName.c_str(), configStorage.getString(e.key.c_str()));
                break;
            default:
                Serial.printf("  key: %-16s type: %-4s\n", e.key.c_str(), e.typeName.c_str());
                break;
        }
    }

    Serial.println();
    Serial.printf("Всього записів: %d\n", entries.size());
    Serial.println("============================================================\n");
}

void dumpSDlistDir(const char* dirname, uint8_t levels) {
    Serial.printf("Вміст директорії: %s\n", dirname);

    File root = SD.open(dirname);
    if (!root || !root.isDirectory()) {
        Serial.println("  (не вдалось відкрити директорію)");
        return;
    }

    File file = root.openNextFile();
    int maxFiles = 50;
    while (file && --maxFiles) {
        if (file.isDirectory()) {
            Serial.printf("  DIR : %-30s       ****\n", file.name());
            if (levels) {
                dumpSDlistDir(file.path(), levels - 1);
            }
        } else {
            Serial.printf("  FILE: %-30s SIZE: %u\n", file.name(), file.size());
        }
        file = root.openNextFile();
    }
    if (file && !maxFiles) {
        Serial.println("  ...");
    }
}

void dumpSDInfo() {
  // 1. Деактивируем выбор других устройств на шине
  //digitalWrite(15, HIGH); // Отключаем TFT_CS
  //digitalWrite(33, HIGH); // Отключаем TOUCH_CS
  //digitalWrite(5, HIGH);  // SD_CS = HIGH (пока отключен)

  Serial.println(F("\n========= SD Card Info ====================================="));

  uint8_t cardType = SD.cardType();

  if (cardType == CARD_NONE) {
    Serial.println(F("❌ Картку не вставлено (або тип не визначено)."));
    Serial.println(F("============================================================\n"));
    return;
  }

  Serial.println(F("✅ Картку успішно знайдено!"));

  // Виводимо тип для деталізації
  Serial.print("Тип картки: ");
  if (cardType == CARD_MMC) Serial.println("MMC");
  else if (cardType == CARD_SD) Serial.println("SDSC");
  else if (cardType == CARD_SDHC) Serial.println("SDHC");
  else Serial.println(F("Невідомий тип"));

  Serial.println(F("------------------------------------------------------------\n"));
  dumpSDlistDir("/", 2);
  Serial.println(F("------------------------------------------------------------\n"));

  // Виводимо розмір картки
  uint64_t cardSize = SD.cardSize() / (1024 * 1024);
  // Serial.printf(F("Розмір картки: %llu MB\n"), cardSize);
  Serial.printf(F("Розмір картки: %s\n"), SizeFormatter::format(SD.cardSize()));
  Serial.printf(F("Зайнято місця: %s (%.2f%%)\n"), SizeFormatter::format(SD.usedBytes()), SD.usedBytes() * 100.0 / SD.cardSize());
  Serial.printf(F("Вільно місця:  %s (%.2f%%)\n"), 
    SizeFormatter::format(SD.cardSize() - SD.usedBytes()),
    (SD.cardSize() - SD.usedBytes()) * 100.0 / SD.cardSize()
  );

  Serial.println(F("============================================================\n"));
}

void dumpLittleFSInfo() {
    Serial.println(F("\n========= LittleFS INFO ===================================="));

    // --- Список усіх файлів ---
    File root = LittleFS.open("/");
    File file = root.openNextFile();
    while (file) {
        Serial.printf("File: %-28s %8d bytes (%s)\n", file.name(), file.size(), SizeFormatter::format(file.size()));
        file = root.openNextFile();
    }

    size_t usedBytes = LittleFS.usedBytes();
    size_t totalBytes = LittleFS.totalBytes();
    double freePercent = ((totalBytes - usedBytes) * 100.00 / totalBytes);

    // --- Скільки місця залишилось ---
    Serial.printf("\nUsed: %d / Total: %d / Free: %d bytes | Free: %.3f%%\n", usedBytes, totalBytes, totalBytes - usedBytes, freePercent);
    Serial.println(F("============================================================\n"));
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
    } else if (section.equals("sd")) {
        SDCardInspector::printAll(SD, Serial);
        // SDCardInspector::printAll(SD_MMC, Serial);
    } else if (section.equals("sd+")) {
        dumpSDInfo();
    } else {
        Serial.println(F("Використання: status sys|cfg|sd|sd+|flash|flash+|littlefs"));
    }
    Serial.print("> ");
}

void setupSerialCommander() {
    commandHandler.registerCommand("status", "Показати статус пристрою: status sys|cfg|sd|flash|flash+|littlefs", [](const String& args) {
        dumpStatus(args);
    });

    commandHandler.registerCommand("reboot", "Перезавантажити пристрій", [](const String& args) {
        dispatcher.dispatch(EVT_REBOOT);
        SystemReset::reboot();
    });

    commandHandler.registerCommand("scan", "Сканувати wi-fi мережі", [](const String& args) {
        WiFi_scan();
    });

    commandHandler.registerCommand("flip", "перевернути екран", [](const String& args) { display_flip(); });

    commandHandler.registerCommand("led", "Керування світлодіодом: led on|off", [](const String& args) {
        if (args.equalsIgnoreCase("on")) {
            Serial.println(F("LED увімкнено"));
        } else if (args.equalsIgnoreCase("off")) {
            Serial.println(F("LED вимкнено"));
        } else {
            Serial.println(F("Використання: led on|off"));
        }
    });

    commandHandler.registerCommand("brightness", "Керування яскравістю: brightness 0-100", [](const String& args) {
        if (args.length() == 0) {
            Serial.println(F("Використання: brightness 0-100|auto"));
        } else if (args.equalsIgnoreCase("auto")) {
            #if LIGHT_SENSOR_PIN > 0
            display.brightness(lightSensor.value());
            configStorage.setBool(CFG_SYS_AUTOBRIGHTNESS, isAutoBrightness = true);
            configStorage.setInt(CFG_DISPLAY_BRIGHTNESS, display.brightness());
            Serial.printf("[SerialCommander] isAutoBrighness = %s\n", isAutoBrightness ? "true" : "false");
            #else
            Serial.printf("[SerialCommander] isAutoBrighness **disabled**\n");
            #endif
        } else {
            configStorage.setBool(CFG_SYS_AUTOBRIGHTNESS, isAutoBrightness = false);
            display.brightness(args.toInt());
            configStorage.setInt(CFG_DISPLAY_BRIGHTNESS, display.brightness());
            Serial.printf("[SerialCommander] display.brightness(%d)\n", display.brightness());
        }
    });

    Serial.println("SerialCommander setup done");
 }

void setupBackgroundImage() {
    #if defined(LITTLEFS_BACKGROUND_IMAGE)
    spaceImage.loadFromLittleFS(LITTLEFS_BACKGROUND_IMAGE, SPRITE_COLOR_DEPTH > 8 ? JpegColorDepth::RGB565 : JpegColorDepth::RGB332);
    setBackgroundImage(spaceImage);
    #endif
}

void setupConfigStorage() {
    configStorage.begin(PIO_PIOENV);
    Serial.println("ConfigStorage init done");
}

void loadConfig() {
    isAutoBrightness = configStorage.getBool(CFG_SYS_AUTOBRIGHTNESS, false);
    display.brightness(configStorage.getInt(CFG_DISPLAY_BRIGHTNESS, 50));
    Serial.println("ConfigStorage load done");
    Serial.printf("\t- %s = %s\n", CFG_SYS_AUTOBRIGHTNESS, isAutoBrightness ? "true" : "false");
    Serial.printf("\t- %s = %d\n", CFG_DISPLAY_BRIGHTNESS, configStorage.getInt(CFG_DISPLAY_BRIGHTNESS, 50));
    Serial.println();
}

void setupEventDispatcher() {
    Serial.println("EventDispatcher setup done");
}

void setupTaskCommander() {
}

void setupLightSensor() {

    #if LIGHT_SENSOR_PIN > 0
        lightSensor.begin();
        scheduler.addCronTask(0, []() { lightSensor.update(); });

        lightSensor.addListener([]() {
            Serial.printf("lightSensor.value() = %4d (%3d%%)", lightSensor.read(), lightSensor.value());
            if (isAutoBrightness) {
                display.brightness(lightSensor.value());
                Serial.printf(" / display.brightness(%d)", lightSensor.value());
            }
            Serial.println();
        });

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

        scheduler.addCronTask(0, []() {
            display.setTextSize(1);
            display.setTextColor(TFT_DARKGREY);
            display.setCursor(10, 240 - 1 * (5 +  display.fontHeight()));
            display.printf("LightSensor: %4d (%3d%%)", lightSensor.read(), lightSensor.value());
        });
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
    //mailer.sendEmail("nick.lavrik@gmail.com", PIO_PIOENV, "hhhh");
    #endif
    display.drawText(10, 10 + 4 * (3 + display.fontHeight()), "SMTP sendmail (done)", TFT_LIGHTGREY);
    display.flush();
}

void drawSystemInfo() {
  uint8_t row = 0;
  // img.fillRect(0, 30, 320, 65, BG_COLOR);

  uint32_t freeHeap = ESP.getFreeHeap();
  uint32_t totalHeap = ESP.getHeapSize();
  int heapPercent = (freeHeap * 100) / totalHeap;

  uint32_t cpuFreq = ESP.getCpuFreqMHz();
  uint32_t uptimeSec = millis() / 1000;

  display.setTextSize(1);
  display.setTextColor(TFT_DARKGREY);

  display.setCursor(10, 10 + row++ * (5 +  display.fontHeight()));
  display.printf(F("Uptime: %02d:%02d:%02d"), uptimeSec / 3600, (uptimeSec / 60) % 60, uptimeSec % 60);

  display.setCursor(10, 10 + row++ * (5 +  display.fontHeight()));
  display.printf(F("CPU: %d MHz   Loop rate: %d/s"), cpuFreq, display.loopFrameRate());

  display.setCursor(10, 10 + row++ * (5 +  display.fontHeight()));
  display.printf("Heap free: %d KB / %d KB (%d%%)", freeHeap / 1024, totalHeap / 1024, heapPercent);

  char* dumpPingStr = dumpPingStatsStr();
  if (dumpPingStr) {
    display.setCursor(10, 10 + row++ * (5 +  display.fontHeight()));
    display.print(dumpPingStatsStr());
  }

  char brightnessStr[200];
  sprintf(brightnessStr, "Brigtness: %d%% %s", display.brightness(), isAutoBrightness ? "(auto)" : "");
  display.setCursor(10, 10 + row++ * (5 +  display.fontHeight()));
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

void setup() {
    setupSerial();
    setupSD();

    setupEventDispatcher();
    setupConfigStorage();
    setupSerialCommander();
    setupLittleFS();
    setupDisplay();
    setupTouchScreen();
    setupWiFi();
    setupNtpService();
    setupBackgroundImage();
    setupTaskCommander();
    setupLightSensor();
    setupMqttClient();
    loadConfig();

    display.flush();

    #if defined(BOOT_BUTTON_PIN)
    pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP); // Enable pull-up resistor
    #endif

    Serial.println("\n> Ready. Введіть 'list' для перегляду команд.\n");
}

void loop() {
    #if defined(BOOT_BUTTON_PIN)
    static bool bootButtonPressed = false;
    int buttonState = digitalRead(BOOT_BUTTON_PIN);
    if ((buttonState == LOW) && !bootButtonPressed) {
        bootButtonPressed = true;
        display_flip();
        Serial.println("Button pressed!");
    } else if (buttonState == LOW) {
        // loop ....
    } else {
        bootButtonPressed = false;
    }
    #endif

    commandHandler.update();
    mqtt.loop();

    // display.startWrite();
    doPing();
    display.clear();
    drawBackgroundImage();
    drawSystemInfo();
    drawTime();

    // sendEmail();

    scheduler.loop();
    touchController.update();

    // display.endWrite();
    display.flush();

    delay(10);
}
