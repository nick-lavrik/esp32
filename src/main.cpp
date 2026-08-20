// main.cpp
//
// esptool --port /dev/ttyUSB0 --after hard-reset chip-id
// python3 -m serial.tools.miniterm --echo --non-exclusive /dev/ttyUSB0 115200
// docker run --rm -it -p 1883:1883 -p 9883:9883 eclipse-mosquitto mosquitto -v -c /mosquitto/config/mosquitto.conf
// mosquitto_sub -h broker.hivemq.com -p 1883 -t "mykola-lavryk/#" -F "@Y-@m-@d @H:@M:@S [%q/%r] %-50t %p" # qos/retain
// mosquitto_pub -h 192.168.1.71 -p 1883 -t mykola-lavryk/command/mqtt-esp32-c6 -m "clock off"
// mosquitto_pub -h broker.hivemq.com -p 1883 -t mykola-lavryk/command/mqtt-esp32-c6-lcd096 -m "clock on"
// mosquitto_pub -h broker.hivemq.com -p 1883 -t mykola-lavryk/command/mqtt-ttgo-t1 -m "clock on"
//
// cls && export PORT=/dev/ttyACM1 && pio run --monitor-port $PORT --upload-port $PORT -e ttgo-t1 -t upload -t monitor
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
// #include <PubSubClient.h>
#include <TouchScreenConfig.h>
#if BOARD_HAS_IMU
#include <ImuController.h>
#endif

#include <AnalogSensor.hpp>
#include <ConfigStorage.hpp>
#include <EspPartitionInspector.hpp>
#include <EventDispatcher.hpp>
#include <GmailSender.hpp>
#include <HttpServer.hpp>
#include <ImageEffects.hpp>
#include <JpegImage.hpp>
#include <LittleFsStaticSource.hpp>
#include <Logger.hpp>
#include <MqttClient.hpp>
#include <MqttKeyGenerator.hpp>
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
#include "strip.h"

#if BOARD_HAS_TOUCHSCREEN
#include <TouchController.h>
#endif

const char* EVT_REBOOT = "reboot";
const char* CFG_SHOW_CLOCK = "clock";
const char* CFG_BLINK_LED = "blink";  // ESP8266 BLINK_LED_PIN dependency
const char* CFG_SYS_AUTOBRIGHTNESS = "auto-brightness";
const char* CFG_DISPLAY_BRIGHTNESS = "brightness";
// runtime override для MQTT_TOPIC_PREFIX (напр. dev/prod/qa/local, регіон, тощо); порожнє
// -> дефолт з secrets.ini
const char* CFG_MQTT_TOPIC_PREFIX = "mqtt.prefix";

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

#ifdef BOARD_ESP32_C6
  // AXS5106L віддає координати в НАТИВНИХ осях панелі (172 x 320), а екран
  // працює в landscape (TFT_ROTATION=3), тобто 320 x 172 - звідси swapXY.
  //
  // Калібрування знято на живій платі по двох кутах:
  //   лівий верхній  -> сирі (164, 312)   (максимуми обох осей)
  //   правий нижній  -> сирі (6, 11)      (мінімуми обох осей)
  // Обидві осі йдуть у зворотному напрямку, тому invertX і invertY.
  // Невеликий недобір до країв (6..164 замість 0..171) - це фізичні поля
  // панелі, спеціально розтягувати діапазон не варто: краї все одно
  // дотискаються обрізанням у TouchPointMapper.
  c.rawMinX = 0;
  c.rawMaxX = TFT_WIDTH;   // 172, нативна ширина панелі
  c.rawMinY = 0;
  c.rawMaxY = TFT_HEIGHT;  // 320, нативна висота панелі

  c.screenWidth = TFT_HEIGHT;   // 320 - екран у landscape
  c.screenHeight = TFT_WIDTH;   // 172

  c.swapXY = true;
  c.invertX = true;
  c.invertY = true;

  c.edgeZoneX = 30;
  c.edgeZoneY = 20;  // менше за X: по висоті всього 172 px
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

#if HAS_MQTT_CLIENT
MqttConfig makeMqttConfig() {
  MqttConfig config;
  config.host = MQTT_HOST, config.port = MQTT_PORT, config.clientId = MQTT_CLIENT_ID;
  config.username = MQTT_USERNAME;
  config.password = MQTT_PASSWORD;
  config.lwtTopic = MQTT_LWT_TOPIC;
  config.lwtOfflineMessage = MQTT_LWT_MSG_OFFLINE;
  config.lwtOnlineMessage = MQTT_LWT_MSG_ONLINE;
  config.prefix = MQTT_TOPIC_PREFIX;  // build-time дефолт; runtime override - setupMqttClient()

  return config;
}
#endif

bool showClock = true;
bool isAutoBrightness = false;

NtpService ntp;
EventDispatcher dispatcher;
TaskController scheduler;
ConfigStorage configStorage;
JpegImage spaceImage;
SerialCommander commandHandler;
WiFiClient wifiClient;
// PubSubClient client(wifiClient);

#if HAS_MQTT_CLIENT
MqttClient mqtt(makeMqttConfig());
// runtime override поверх MqttConfig::prefix; заповнюється лише за наявності
// CFG_MQTT_TOPIC_PREFIX в ConfigStorage, див. setupMqttClient()
MqttKeyGenerator mqttTopicPrefixOverride;
#endif

LittleFsStaticSource littleFsSource(LittleFS);
HttpServer httpServer(HttpServerConfig{});
//NetworkSupervisor wifi;

// Хост і base64(login:password) приходять із secrets.ini через build_flags
// (ROUTER_HOST / ROUTER_LOGIN_AUTHORIZATION) - раніше вони були захардкожені
// тут, у файлі під git, попри те що механізм для секретів уже існував.
RouterApiClient routerApi(ROUTER_HOST, ROUTER_LOGIN_AUTHORIZATION);

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
// За замовчуванням дотики йдуть у debug, а DEFAULT_LOG_LEVEL=3 (info) їх не
// пропускає - тобто в консолі порожньо навіть коли тач справний. Прапорець
// нижче (команда "touchlog on") піднімає їх до info на час налагодження, не
// засмічуючи звичайний вивід.
bool touchLogVerbose = false;

void onTouchLog(TouchPoint p) {
  if (touchLogVerbose) {
    Logger::info("Touch: %d, %d", p.x, p.y);
  } else {
    Logger::debug("Touch: %d, %d", p.x, p.y);
  }
}
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


void testAsusWRT() {
  Logger::info("====== AsusWRT test script =======");
  Logger::info("free heap: %u", ESP.getFreeHeap());

  const char* path = "/asus-get_clientlist.json";
  File file = LittleFS.open(path, "r");
  if (!file || file.isDirectory()) {
    Logger::error("Can't open file (%s)", path);
    return;
  }

  // Logger::info("free heap before read: %u", ESP.getFreeHeap());
  String json = file.readString();
  file.close();
  // Logger::info("free heap after read (json size=%u): %u", json.length(), ESP.getFreeHeap());

  std::vector<RouterClientInfo> clients;
  if (!RouterClientListParser::parse(json, clients)) {
    Logger::error("can't parse client list json. [%d]", clients.capacity());
  }

  // Logger::info("free heap after parse: %u", ESP.getFreeHeap());

  RouterClientListIterator it(std::move(clients));
  while (it.hasNext()) {
    const RouterClientInfo& c = it.next();
    Logger::info("client.name=%s", c.name.c_str());
  }
  Logger::info("------ AsusWRT test script -------");
  Logger::info("");
}

// Застосувати яскравість БЕЗ запису в NVS.
void display_brightness_apply(uint8_t percent, bool _auto) {
  display.brightness(percent);
  isAutoBrightness = _auto;
}

// Застосувати ТА зберегти в NVS. Викликати лише для явних дій користувача
// (команда, свайп, кнопка).
//
// В авто-режимі значення змінюється на кожну зміну показань сенсора (гістерезис
// 5%), і раніше кожна з них давала ДВА записи в NVS - це пряме зношування flash
// (у NVS обмежена кількість циклів стирання). Зберігати там нічого й не
// потрібно: на старті яскравість в авто-режимі однаково перераховується з
// сенсора. Тому слухач сенсора користується display_brightness_apply().
void display_brightness(uint8_t percent, bool _auto) {
  display_brightness_apply(percent, _auto);
  configStorage.setInt(CFG_DISPLAY_BRIGHTNESS, display.brightness());
  configStorage.setBool(CFG_SYS_AUTOBRIGHTNESS, isAutoBrightness);
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

// I2C-шина СПІЛЬНА для тача й IMU, тому Wire.begin() робиться рівно один раз
// тут, а не в кожному драйвері: повторний Wire.begin() з тими самими пінами
// нешкідливий, але з РІЗНИМИ - мовчки переприв'язує шину і ламає той
// пристрій, що ініціалізувався першим.
void setupI2C() {
  #if defined(I2C_SDA) && defined(I2C_SCL)
  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(400000);
  Logger::info("I2C: SDA=%d SCL=%d @400kHz", I2C_SDA, I2C_SCL);
  #endif
}

// Скан шини. Потрібен, бо документація і сторонні драйвери розходяться в
// адресах (AXS5106L: 0x51 у Waveshare FAQ проти 0x63 у toto04/axs5106l),
// а єдиний спосіб дізнатися правду - спитати саму плату.
void i2cScan() {
#if defined(I2C_SDA) && defined(I2C_SCL)
  Logger::info("========= I2C scan (SDA=%d SCL=%d) =========================", I2C_SDA, I2C_SCL);

  int found = 0;
  for (uint8_t addr = 0x08; addr < 0x78; ++addr) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      ++found;
      const char* known = "";
      if (addr == 0x6B || addr == 0x6A) known = " <- QMI8658A (IMU)";
      else if (addr == 0x63 || addr == 0x51) known = " <- AXS5106L (touch)";
      Logger::info("  0x%02X%s", addr, known);
    }
  }

  Logger::info("------------------------------------------------------------");
  Logger::info(found ? "Знайдено пристроїв: %d" : "Нічого не знайдено - перевір піни/живлення", found);
  Logger::info("============================================================");
#else
  Logger::error("I2C diabled (I2C_SDA / I2C_SCL not defined)");
#endif
}

void setupImu() {
#if BOARD_HAS_IMU
  if (ImuController::setup()) {
    Logger::info("IMU setup done");
  }
#endif
}

// Переворот плати догори дриґом перевертає й зображення, і навпаки.
//
// Стан порівнюється з ПОПЕРЕДНІМ, а не з абсолютною орієнтацією: flip()
// перемикає поточний поворот на 180 градусів, тому реагувати треба саме на
// ЗМІНУ, інакше кожен виклик у FaceDown крутив би екран нескінченно.
// Стартова орієнтація фіксується як базова і сама по собі flip не викликає -
// плата, увімкнена вже перевернутою, показує звичайний екран.
void updateImuFlip() {
#if BOARD_HAS_IMU
  static ImuController::Orientation last = ImuController::Orientation::Unknown;

  ImuController::update();
  const ImuController::Orientation now = ImuController::orientation();

  if (now == ImuController::Orientation::Unknown) return;


  if (last == ImuController::Orientation::Unknown && now == ImuController::Orientation::TopUp) {
    last = now;  // базова орієнтація зі старту, без flip
    return;
  }
  
  if (now == last) return;

  last = now;
  Logger::info("[IMU] орієнтація змінилась: %s (%s=%.2fg) -> flip",
               ImuController::orientationName(now), ImuController::upAxisName(),
               ImuController::upAxisValue());

  display_flip();
#endif
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
  #if HAS_MQTT_CLIENT
  static TLogger _logger{"mqtt"};

  // Runtime override - лише якщо реально збережено в ConfigStorage; інакше mqtt сам
  // застосує _config.prefix (build-time дефолт з secrets.ini) через _defaultKeyGenerator.
  String storedPrefix = configStorage.getString(CFG_MQTT_TOPIC_PREFIX, "");
  if (storedPrefix.length() > 0) {
    mqttTopicPrefixOverride.setPrefix(storedPrefix.c_str());
    mqtt.setKeyGenerator(&mqttTopicPrefixOverride);  // ДО begin()
  }

  mqtt.begin();
  _logger.info("topic prefix = '%s'", mqtt.keyGenerator().prefix().c_str());

  // mqtt.publish(MQTT_LWT_TOPIC, "dummy-init-message", 1);
  scheduler.addCronTask(5 * 60 * 1000UL, []() { mqtt.publish(MQTT_LWT_TOPIC, "hearbeat"); });

  /* #if !BOARD_ESP32_C6 || true
  mqtt.addStringListener("#", [](const char* topic, const char* payload) {
    // char t[9] = ""; ntp.ftime("%H:%M:%S", t, sizeof(t));
    _logger.debug(">>> %-50s %s", topic, payload);
  });
  #endif */

  #if !BOARD_ESP32_C6 || true
  mqtt.addStringListener("command/" MQTT_CLIENT_ID, [](const char* topic, const char* payload) {
    // char t[9] = ""; ntp.ftime("%H:%M:%S", t, sizeof(t));
    _logger.warn("command %s", payload);
    commandHandler.execute(payload);
  });
  #endif

  // LWT_TOPIC "mykola-lavryk:devices/${PIOENV}/status"
  mqtt.addStringListener("devices/+/status", [](const char* topic, const char* payload) {
    char t[9] = ""; ntp.ftime("%H:%M:%S", t, sizeof(t));
    _logger.info("%s %-45.45s LWT:%s", t, topic, payload);
  });

  dispatcher.addListener(EVT_REBOOT, [](IEvent& e) { mqtt.disconnect("reboot"); });

#if LIGHT_SENSOR_PIN > 0
  // publish mqtt
  lightSensor.addListener([]() {
      _logger.debug("devices/" PIO_PIOENV "/light-sensor => %d", lightSensor.value());
      mqtt.publishNumber<int>("devices/" PIO_PIOENV "/light-sensor", (int)lightSensor.value());
  });
  _logger.info("devices/" PIO_PIOENV "/light-sensor MQTT done.");
#else
  // subscribe on mqtt
  mqtt.addNumberListener<int>(
    "devices/+/light-sensor",
    [](const char* topic, int value) {
      char t[9] = ""; ntp.ftime("%H:%M:%S", t, sizeof(t)); 
      _logger.info("%s %-45s val:%d%%", t, topic, value); 
    });
  _logger.info("devices/+/light-sensor listen");
#endif

  commandHandler.registerCommand("dump-mqtt", "show MQTT status", [](const String args) {
    _logger.info("isConnected = %s, topic prefix = '%s'", mqtt.isConnected() ? "yes" : "no",
                 mqtt.keyGenerator().prefix().c_str());
  });

  commandHandler.registerCommand(
    "publish", "publish message in MQTT: publish <topic> <message>",
    [](const String args) {
      if (args.length() == 0) {
        _logger.info("use: publish <topic> <payload>");
        return;
      }

      int spaceIdx = args.indexOf(' ');
      if (spaceIdx < 0) {
        _logger.info("use: publish <topic> <payload>");
        return;
      }

      String topic = args.substring(0, spaceIdx);
      String message = args.substring(spaceIdx + 1);
      message.trim();
      bool ok = mqtt.publish(topic.c_str(), message.c_str());
      _logger.info("publish (%s:%s) %s", topic.c_str(), message.c_str(), ok ? "success" : "fail");
    }
  );

  commandHandler.registerCommand(
    "mqtt-prefix", "get/set MQTT topic prefix (eg. dev/prod/qa/eu-west1): mqtt-prefix [prefix]",
    [](const String args) {
      if (args.length() == 0) {
        _logger.info("mqtt topic prefix = '%s'", mqtt.keyGenerator().prefix().c_str());
        return;
      }
      configStorage.setString(CFG_MQTT_TOPIC_PREFIX, args);
      _logger.info("saved '%s' -> reboot required to take effect (topics already "
                    "subscribed with old prefix)",
                    args.c_str());
    }
  );

  /*
  static uint32_t i = 0;
  mqtt.addNumberListener<uint32_t>(
    "int32/#",
    [](const char* t, uint32_t v) {
      char w[9] = ""; ntp.ftime("%H:%M:%S", w, sizeof(w)); 
      _logger.info("%s %-45.45s int:%d", w, t, v); 
    });

  scheduler.addCronTask(
    1 * 60 * 1000UL,
    []() { mqtt.publishNumber<uint32_t>("int32/" MQTT_CLIENT_ID, (uint32_t)++i); }
  );
  */

  _logger.info("%s:%d (%s) lwt:%s", MQTT_HOST, MQTT_PORT, MQTT_CLIENT_ID,
               mqtt.keyGenerator().key(MQTT_LWT_TOPIC).c_str());
  #else
  Logger::warn("MQTT client disabled!");
  #endif
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
  // --- SPI-гілка ---------------------------------------------------------
  // На esp32-c6-lcd096 шина СПІЛЬНА з дисплеєм (SCK=7, MOSI=6), окремі
  // CS: 4 (SD) / 14 (LCD). setupSD() навмисно викликається ПЕРЕД
  // setupDisplay() (див. setup()), тому на цей момент TFT_CS ще НЕ
  // сконфігурований драйвером дисплея і висить плаваючим входом. ST7735
  // write-only і сам MISO не тягне, але поки його CS не підтягнутий у
  // HIGH, він приймає весь init-трафік картки як власні команди - і
  // залишає дисплей у невизначеному стані. Тому деактивуємо всі інші CS
  // шини ДО першого такту SCK.
#if defined(TFT_CS) && (TFT_CS >= 0)
  pinMode(TFT_CS, OUTPUT);
  digitalWrite(TFT_CS, HIGH);
#endif

  // CS картки має бути HIGH ще до SPI.begin(): за специфікацією SD картка
  // переходить у SPI-режим лише побачивши >=74 такти при деактивованому CS.
  pinMode(SD_CS, OUTPUT);
  digitalWrite(SD_CS, HIGH);

  // Внутрішній підтяг на MISO: на дешевих платах зовнішнього резистора на
  // лінії DO картки часто немає, і поки картка не вибрана, лінія "висить" -
  // контролер читає сміття замість 0xFF і CMD0/CMD8 не проходять.
  pinMode(SD_MISO, INPUT_PULLUP);

  SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);

  Logger::info("SD (SPI): CS=%d SCK=%d MOSI=%d MISO=%d", SD_CS, SD_SCK, SD_MOSI, SD_MISO);

  // Сходинки частот: СПОЧАТКУ робоча SD_FREQ, і лише якщо вона не
  // піднялась - 400 кГц (частота ініціалізації за специфікацією SD) як
  // запасний варіант. Порядок принциповий: перша успішна сходинка стає
  // робочою частотою шини на весь сеанс, тому 400 кГц першою означала б
  // вдесятеро повільніший SD навіть там, де 4 МГц працюють.
  // Якщо в лозі видно fail на SD_FREQ і успіх на 400 кГц - проблема в
  // якості шини (спільна з дисплеєм), а не в пінах чи картці.
  const uint32_t freqs[] = {(uint32_t)SD_FREQ, 400000UL};
  const int maxAttempts = 3;

  for (uint32_t freq : freqs) {
    for (int attempt = 1; attempt <= maxAttempts; ++attempt) {
      if (SD.begin(SD_CS, SPI, freq)) {
        Logger::info("SD card init done (%u Hz, спроба %d/%d)", (unsigned)freq, attempt, maxAttempts);
        return;
      }
      delay(100);
    }
    Logger::warn("SD init fail @ %u Hz", (unsigned)freq);
  }

  Logger::error("SD init fail. Перевір: картка вставлена? FAT32/FAT16 (НЕ exFAT, НЕ >32GB)? піни?");
#endif
#else
  Logger::warn("SD disabled.");
#endif

  return;
}

int getWiFiQuality(long rssi) {
  if (rssi >= -50) return 100;
  if (rssi <= -100) return 0;
  
  // Лінійна інтерполяція між -100 dBm (0%) та -50 dBm (100%)
  return map(rssi, -100, -50, 0, 100); 
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
    // Було: два специфікатори на ОДИН аргумент, до того ж double під %d.
    Logger::info("Free PSRAM:  %d bytes (%.2f Mb)", ESP.getFreePsram(),
                 ESP.getFreePsram() / 1024.0 / 1024.0);
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
  Logger::info("WiFi SSID: %s (%d dBm / %d%%)", WiFi.SSID().c_str(), WiFi.RSSI(), getWiFiQuality(WiFi.RSSI()));
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
  Logger::info("Total records: %d", entries.size());
  Logger::info("============================================================");
}

#if BOARD_HAS_SD
// ---------------------------------------------------------------------------
// YIELD_DISPLAY_BUS() - RAII-дужка, яка тимчасово віддає SPI-шину дисплея
// на час звернення до TF-картки.
//
// НАВІЩО: loop() тримає транзакцію дисплея відкритою через УВЕСЬ кадр
// (display.startWrite() ... display.endWrite()), і саме всередині неї
// викликаються commandHandler.update() та mqtt.loop() - тобто будь-яка
// консольна чи MQTT-команда виконується з-під відкритої транзакції.
//
// На платах, де дисплей і картка сидять на ОДНІЙ SPI-шині
// (SD_SHARES_DISPLAY_SPI), це смертельно: SPIClass::beginTransaction()
// бере НЕ рекурсивний мьютекс paramLock з portMAX_DELAY
// (framework-arduinoespressif32, libraries/SPI/src/SPI.cpp), а драйвер
// картки робить beginTransaction() на КОЖНУ операцію (libraries/SD/src/
// sd_diskio.cpp, struct AcquireSPI). Другий take того самого мьютекса з
// того самого потоку - вічний дедлок, плата зависає намертво.
//
// Асиметрія викликів нижче навмисна:
//   endWrite()   - безпечно викликати зайвий раз: SPIClass::endTransaction()
//                  захищений прапорцем _inTransaction, а CS дисплея просто
//                  піднімається в HIGH (що нам тут і потрібно);
//   startWrite() - НЕ можна викликати двічі поспіль: Arduino_TFT::startWrite()
//                  не має лічильника вкладеності і напряму робить
//                  beginWrite() -> той самий дедлок. Тому він тут рівно
//                  один, у деструкторі.
//
// ОБМЕЖЕННЯ: дужка розрахована на виклик З-ПІД відкритої транзакції (тобто
// з loop()). Якщо застосувати її у setup() - деструктор залишить транзакцію
// дисплея відкритою. У setupSD() її тому й немає.
// ---------------------------------------------------------------------------
#if defined(SD_SHARES_DISPLAY_SPI) && SD_SHARES_DISPLAY_SPI
struct DisplayBusYield {
  DisplayBusYield() { tft.endWrite(); }
  ~DisplayBusYield() { tft.startWrite(); }
};
#define YIELD_DISPLAY_BUS() DisplayBusYield _displayBusYield_
#else
#define YIELD_DISPLAY_BUS() ((void)0)
#endif

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
 
  YIELD_DISPLAY_BUS();

  Logger::info("========= SD Card Info =====================================");
 
  uint8_t cardType = ACTIVE_SD.cardType();
 
  if (cardType == CARD_NONE) {
    Logger::info("❌ Card not found (or type not detected).");
    Logger::info("============================================================");
    return;
  }
 
  Logger::info("✅ Card found!");
 
  // Виводимо тип для деталізації
  if (cardType == CARD_MMC)
    Logger::info("ТCard type: %s", "MMC");
  else if (cardType == CARD_SD)
    Logger::info("Card type: %s", "SDSC");
  else if (cardType == CARD_SDHC)
    Logger::info("Card type: %s", "SDHC");
  else
    Logger::info("Card type: %s", "UnknownType");
 
  Logger::info("------------------------------------------------------------");
  dumpSDlistDir("/", 2);
  Logger::info("------------------------------------------------------------");
 
  // Виводимо розмір картки
  uint64_t cardSize = ACTIVE_SD.cardSize() / (1024 * 1024);
  // Serial.printf(F("Розмір картки: %llu MB\n"), cardSize);
  Logger::info("Card size: %s", SizeFormatter::format(ACTIVE_SD.cardSize()));
  Logger::info("Used: %s (%.2f%%)", SizeFormatter::format(ACTIVE_SD.usedBytes()),
               ACTIVE_SD.usedBytes() * 100.0 / ACTIVE_SD.cardSize());
  Logger::info("Free:  %s (%.2f%%)", SizeFormatter::format(ACTIVE_SD.cardSize() - ACTIVE_SD.usedBytes()),
               (ACTIVE_SD.cardSize() - ACTIVE_SD.usedBytes()) * 100.0 / ACTIVE_SD.cardSize());
 
  Logger::info("============================================================");
}
 
#undef ACTIVE_SD

#if !defined(BOARD_ESP32_S3_LCD147)
// ---------------------------------------------------------------------------
// Низькорівневий пробник TF-картки по SPI (команда "sdprobe").
//
// НАВІЩО: SD.begin() повертає лише bool, і за ним неможливо відрізнити
// три принципово різні причини відмови:
//   1) картка взагалі не відповідає  -> помилка в пінах/проводці/живленні;
//   2) картка відповідає на CMD0/CMD8 -> шина справна, а не монтується
//      файлова система (exFAT/пошкоджений розділ/картка >32GB);
//   3) відповідає лише на низькій частоті -> проблема з якістю шини
//      (тут вона СПІЛЬНА з дисплеєм).
// Пробник шле CMD0 (GO_IDLE_STATE) і CMD8 (SEND_IF_COND) вручну, як це
// робить сама специфікація SD в SPI-режимі, і друкує сирі R1-відповіді.
// ---------------------------------------------------------------------------

// Одна команда SD в SPI-режимі; повертає R1 (0xFF = картка не відповіла).
// Картка тримає лінію DO (MISO) притиснутою в LOW, поки зайнята - і поки
// вона це робить, будь-яке читання дає 0x00.
//
// САМЕ НА ЦЕ попалась перша версія проби: вона вважала валідною R1 будь-який
// байт зі скинутим бітом 7, а 0x00 цю умову задовольняє. Тому busy-стан
// читався як "CMD0 -> R1=0x00", хоча насправді картка просто ще не
// відпустила лінію після GO_IDLE_STATE, який робить SD.end().
// Повертає false, якщо картка не звільнилась за timeoutMs.
static bool sdProbeWaitReady(uint32_t timeoutMs) {
  uint32_t start = millis();
  do {
    if (SPI.transfer(0xFF) == 0xFF) return true;
  } while (millis() - start < timeoutMs);
  return false;
}

// Одна команда SD у SPI-режимі.
// Повертає R1, або 0xFF - картка не відповіла, або 0xFE - картка не
// звільнила лінію (busy). Обидва службові коди мають виставлений біт 7,
// тому не можуть збігтися з валідною R1.
static uint8_t sdProbeCmd(uint8_t cmd, uint32_t arg, uint8_t crc, uint32_t waitMs = 500) {
  if (!sdProbeWaitReady(waitMs)) return 0xFE;

  SPI.transfer(0xFF);
  SPI.transfer(0x40 | cmd);
  SPI.transfer((uint8_t)(arg >> 24));
  SPI.transfer((uint8_t)(arg >> 16));
  SPI.transfer((uint8_t)(arg >> 8));
  SPI.transfer((uint8_t)arg);
  SPI.transfer(crc);

  // R1 приходить у межах 8 байтів: перший байт, що НЕ 0xFF (шина в спокої)
  // і має скинутий біт 7.
  for (int i = 0; i < 10; ++i) {
    uint8_t r = SPI.transfer(0xFF);
    if (r != 0xFF && !(r & 0x80)) return r;
  }
  return 0xFF;
}

// force=false - неруйнівний режим (за замовчуванням): якщо картка вже
// змонтована, сира проба не виконується взагалі. Причина в тому, що
// відібрати шину у драйвера можна лише через SD.end(), а повторний
// SD.begin() у тому самому сеансі надійно НЕ піднімається (перевірено на
// цій платі: "sdWait: Wait Failed" -> "GO_IDLE_STATE failed" -> f_mount (3)),
// тобто проба коштувала б робочої картки до перезавантаження.
void sdProbe(bool force) {
  YIELD_DISPLAY_BUS();

  Logger::info("========= SD probe (raw SPI) ===============================");
  Logger::info("Піни: CS=%d SCK=%d MOSI=%d MISO=%d", SD_CS, SD_SCK, SD_MOSI, SD_MISO);

  if (SD.cardType() != CARD_NONE && !force) {
    Logger::info("Картка вже змонтована - сира проба не потрібна і не безпечна.");
    Logger::info("  Деталі по картці: status sd");
    Logger::info("  Якщо проба потрібна саме зараз: sdprobe force");
    Logger::info("  (це демонтує картку остаточно, до reboot).");
    Logger::info("============================================================");
    return;
  }

  // Звільняємо шину від драйвера SD (якщо він піднявся) і деактивуємо
  // дисплей - інакше ST7735 їстиме наші такти як свої команди.
  const bool wasMounted = (SD.cardType() != CARD_NONE);
  if (wasMounted) {
    Logger::warn("force: демонтую картку, після проби знадобиться reboot");
    SD.end();
    // sdcard_uninit() шле картці GO_IDLE_STATE - їй треба час відпустити DO.
    delay(50);
  }

#if defined(TFT_CS) && (TFT_CS >= 0)
  pinMode(TFT_CS, OUTPUT);
  digitalWrite(TFT_CS, HIGH);
#endif
  pinMode(SD_CS, OUTPUT);
  digitalWrite(SD_CS, HIGH);

  // Чи тягне лінію DO хтось ЗЗОВНІ (картка або зовнішній резистор)?
  //
  // Читати з INPUT_PULLUP тут БЕЗГЛУЗДО - внутрішній підтяг сам дає HIGH,
  // і тест проходить навіть на піні, до якого нічого не підключено (перша
  // версія проби саме так і брехала). Єдиний спосіб побачити ЗОВНІШНІЙ
  // підтяг - притиснути пін власним pull-down: якщо він усе одно читається
  // як HIGH, значить ззовні його тягне щось сильніше.
  //
  // Те саме стосується і роботи по SPI: spiAttachMISO() робить
  // pinMode(miso, INPUT) БЕЗ підтягу (esp32-hal-spi.c), тож непідключена
  // лінія плаває і читається як 0x00 - неотличимо від "картка зайнята".
  pinMode(SD_MISO, INPUT_PULLDOWN);
  delay(2);
  const bool pulledExternally = digitalRead(SD_MISO);
  pinMode(SD_MISO, INPUT_PULLUP);
  delay(2);
  Logger::info("MISO=%d: зовнішній підтяг %s", SD_MISO,
               pulledExternally ? "Є (картка/резистор на лінії)"
                                : "ВІДСУТНІЙ - на цьому піні найімовірніше нічого немає");

  SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  SPI.beginTransaction(SPISettings(400000, MSBFIRST, SPI_MODE0));

  // >=74 такти при CS=HIGH - обов'язковий перехід картки в SPI-режим.
  for (int i = 0; i < 10; ++i) SPI.transfer(0xFF);

  digitalWrite(SD_CS, LOW);
  uint8_t r1 = sdProbeCmd(0, 0x00000000, 0x95);  // CMD0 GO_IDLE_STATE
  Logger::info("CMD0 (GO_IDLE_STATE) -> R1=0x%02X %s", r1,
               r1 == 0x01   ? "OK (картка в idle)"
               : r1 == 0xFF ? "НЕМАЄ ВІДПОВІДІ - картка/піни/живлення"
               : r1 == 0xFE ? "лінія DO постійно в LOW (не підключена або картка зайнята)"
               : r1 == 0x00 ? "відповіла, але не в idle"
                            : "несподівано");

  // CMD8 має сенс за будь-якої валідної R1 (біт 7 скинутий), не лише 0x01.
  if (!(r1 & 0x80)) {
    uint8_t r8 = sdProbeCmd(8, 0x000001AA, 0x87);  // CMD8 SEND_IF_COND
    uint8_t echo[4] = {0};
    for (int i = 0; i < 4; ++i) echo[i] = SPI.transfer(0xFF);
    Logger::info("CMD8 (SEND_IF_COND) -> R1=0x%02X echo=%02X %02X %02X %02X %s", r8, echo[0], echo[1],
                 echo[2], echo[3],
                 (r8 == 0x01 && echo[3] == 0xAA) ? "OK (SDHC/SDXC v2)"
                 : (r8 & 0x04)                   ? "illegal command (стара SDSC v1)"
                                                 : "несподівано");
  }

  digitalWrite(SD_CS, HIGH);
  SPI.transfer(0xFF);
  SPI.endTransaction();

  Logger::info("------------------------------------------------------------");
  if (r1 == 0xFF) {
    Logger::info("ВИСНОВОК: картка не відповідає взагалі - шукати причину в");
    Logger::info("  пінах (SD_CS/SD_SCK/SD_MOSI/SD_MISO у platformio.ini),");
    Logger::info("  контакті слота або живленні 3V3.");
  } else if (r1 == 0xFE) {
    Logger::info("ВИСНОВОК: лінія DO постійно читається як 0. Два варіанти:");
    Logger::info("  a) SD_MISO не той пін / картки на ньому немає - якщо рядок");
    Logger::info("     про зовнішній підтяг вище каже ВІДСУТНІЙ, то саме це;");
    Logger::info("  b) залишковий busy після демонтування - тоді поможе reboot.");
    Logger::info("  Підібрати піни автоматично: sdscan");
  } else {
    Logger::info("ВИСНОВОК: шина і картка справні. Якщо SD.begin() все одно");
    Logger::info("  падає - справа у файловій системі: потрібен FAT32/FAT16");
    Logger::info("  (exFAT і картки >32GB Arduino-бібліотека SD не монтує).");
  }
  if (wasMounted) {
    Logger::warn("Картку демонтовано пробою - виконай reboot, щоб повернути SD.");
  }
  Logger::info("============================================================");
}

// ---------------------------------------------------------------------------
// sdscan - автопідбір SD_MISO/SD_CS перебором (команда "sdscan").
//
// НАВІЩО: SD_SCK/SD_MOSI спільні з дисплеєм, тому вони вже підтверджені тим,
// що дисплей працює. А SD_MISO/SD_CS ніде більше не задіяні - якщо вони взяті
// з документації "схожої" плати і не збігаються з реальною розводкою, картка
// мовчить, і відрізнити це від несправної картки по логах SD неможливо.
// Скан перебирає пари (CS, MISO) при фіксованих SCK/MOSI і шукає ту, на якій
// CMD0 повертає 0x01.
//
// БЕЗПЕКА: перебираються лише GPIO зі списку нижче. Свідомо ВИКЛЮЧЕНІ піни,
// смикання яких зашкодило б: 12/13 (USB Serial/JTAG - вбило б консоль),
// 16/17 (UART0), 24..30 (шина SPI-флеша), 8/9 (strapping/boot). Піни, зайняті
// дисплеєм і самою шиною, відсіюються в рантаймі нижче.
static const uint8_t SD_SCAN_CANDIDATES[] = {0, 3, 4, 5, 6, 7, 10, 11, 18, 19, 20, 21};

static bool sdScanPinBusy(uint8_t pin) {
  if (pin == SD_SCK || pin == SD_MOSI) return true;
#if defined(TFT_CS) && (TFT_CS >= 0)
  if (pin == TFT_CS) return true;
#endif
#if defined(TFT_DC) && (TFT_DC >= 0)
  if (pin == TFT_DC) return true;
#endif
#if defined(TFT_RST) && (TFT_RST >= 0)
  if (pin == TFT_RST) return true;
#endif
#if defined(TFT_BL) && (TFT_BL >= 0)
  if (pin == TFT_BL) return true;
#endif
  return false;
}

// Одна спроба CMD0 на конкретній парі пінів. Таймаути тут навмисно короткі
// (5 мс): комбінацій понад сотня, а робоча пара відповідає одразу.
static uint8_t sdScanTry(uint8_t cs, uint8_t miso) {
  SPI.end();
  SPI.begin(SD_SCK, miso, SD_MOSI, cs);

  pinMode(cs, OUTPUT);
  digitalWrite(cs, HIGH);

  SPI.beginTransaction(SPISettings(400000, MSBFIRST, SPI_MODE0));
  for (int i = 0; i < 10; ++i) SPI.transfer(0xFF);  // >=74 такти при CS=HIGH

  digitalWrite(cs, LOW);
  uint8_t r1 = sdProbeCmd(0, 0x00000000, 0x95, 5);
  digitalWrite(cs, HIGH);
  SPI.transfer(0xFF);
  SPI.endTransaction();

  return r1;
}

void sdScan() {
  YIELD_DISPLAY_BUS();

  Logger::info("========= SD pin scan ======================================");

  if (SD.cardType() != CARD_NONE) {
    Logger::info("Картка вже змонтована на CS=%d MISO=%d - скан не потрібен.", SD_CS, SD_MISO);
    Logger::info("============================================================");
    return;
  }

  Logger::info("Фіксовані (спільні з дисплеєм, тому вже підтверджені): SCK=%d MOSI=%d", SD_SCK, SD_MOSI);
  Logger::info("Поточні (перевіряються): CS=%d MISO=%d", SD_CS, SD_MISO);

  // Дисплей на тій самій шині - тримаємо його CS у HIGH на весь скан.
#if defined(TFT_CS) && (TFT_CS >= 0)
  pinMode(TFT_CS, OUTPUT);
  digitalWrite(TFT_CS, HIGH);
#endif

  const size_t n = sizeof(SD_SCAN_CANDIDATES);
  int found = 0;
  int tried = 0;

  for (size_t i = 0; i < n; ++i) {
    const uint8_t cs = SD_SCAN_CANDIDATES[i];
    if (sdScanPinBusy(cs)) continue;

    for (size_t j = 0; j < n; ++j) {
      const uint8_t miso = SD_SCAN_CANDIDATES[j];
      if (miso == cs || sdScanPinBusy(miso)) continue;

      ++tried;
      const uint8_t r1 = sdScanTry(cs, miso);

      if (r1 == 0x01) {
        ++found;
        Logger::info("✅ ЗНАЙДЕНО: CS=%d MISO=%d -> CMD0 R1=0x01", cs, miso);
      } else if (!(r1 & 0x80)) {
        // Відповідь є, але не idle - теж вартий уваги кандидат.
        Logger::info("?  CS=%d MISO=%d -> CMD0 R1=0x%02X (відповідь є, але не idle)", cs, miso, r1);
      }
    }
  }

  // Повертаємо шину на штатні піни, інакше дисплей лишиться на пінах з
  // останньої перебраної комбінації.
  SPI.end();
  SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  pinMode(SD_CS, OUTPUT);
  digitalWrite(SD_CS, HIGH);

  Logger::info("------------------------------------------------------------");
  Logger::info("Перебрано комбінацій: %d, знайдено: %d", tried, found);
  if (found) {
    Logger::info("Пропиши знайдену пару в platformio.ini (SD_CS/SD_MISO) і перепрошийся.");
  } else {
    Logger::info("Жодна пара не відповіла. Найімовірніше картка не вставлена,");
    Logger::info("  слот без живлення, або SD_SCK/SD_MOSI на цій платі інші,");
    Logger::info("  ніж піни дисплея (тоді скан із фіксованими SCK/MOSI безсилий).");
  }
  Logger::info("============================================================");
}

// ---------------------------------------------------------------------------
// sdbb - програмна (bit-bang) проба картки, команда "sdbb".
//
// НАВІЩО ще одна проба: sdprobe їде на апаратній SPI-периферії, тому його
// мовчання можна пояснити щонайменше трьома різними причинами - хибні піни,
// GPIO matrix/perimanager віддав пін іншій периферії, або картка справді не
// відповідає. Bit-bang не використовує SPI-периферію взагалі: тільки
// digitalWrite/digitalRead, тому лишає рівно одну можливу причину - залізо.
//
// Друкується СИРИЙ дамп прийнятих байтів, а не лише розібрана R1. Він
// однозначно розрізняє три стани лінії:
//   FF FF FF ... - лінія підтягнута, картка мовчить (немає/не живиться);
//   00 00 00 ... - лінія притиснута в нуль (немає підтягу або вічний busy);
//   будь-що інше - картка на лінії реагує, і далі вже видно як саме.
// ---------------------------------------------------------------------------
static uint8_t sdBitBangTransfer(uint8_t out) {
  uint8_t in = 0;
  for (int i = 7; i >= 0; --i) {
    digitalWrite(SD_MOSI, (out >> i) & 1);
    delayMicroseconds(5);
    digitalWrite(SD_SCK, HIGH);   // дані читаються по наростаючому фронту (SPI mode 0)
    delayMicroseconds(5);
    in = (uint8_t)((in << 1) | (digitalRead(SD_MISO) ? 1 : 0));
    digitalWrite(SD_SCK, LOW);
  }
  return in;
}

static void sdBitBangDump(const char* label, uint8_t* buf, int n) {
  char hex[3 * 12 + 1] = {0};
  int pos = 0;
  for (int i = 0; i < n && pos < (int)sizeof(hex) - 3; ++i) {
    pos += snprintf(hex + pos, sizeof(hex) - pos, "%02X ", buf[i]);
  }
  Logger::info("  %-22s %s", label, hex);
}

void sdBitBang() {
  YIELD_DISPLAY_BUS();

  Logger::info("========= SD bit-bang probe ================================");
  Logger::info("Піни: CS=%d SCK=%d MOSI=%d MISO=%d (без SPI-периферії)", SD_CS, SD_SCK, SD_MOSI, SD_MISO);

  const bool wasMounted = (SD.cardType() != CARD_NONE);
  if (wasMounted) {
    Logger::warn("Картка змонтована - демонтую; після проби знадобиться reboot");
    SD.end();
    delay(50);
  }

  // Забираємо піни в SPI-периферії, інакше pinMode/digitalWrite на них
  // конфліктують з GPIO matrix.
  SPI.end();

#if defined(TFT_CS) && (TFT_CS >= 0)
  pinMode(TFT_CS, OUTPUT);
  digitalWrite(TFT_CS, HIGH);
#endif

  pinMode(SD_SCK, OUTPUT);
  pinMode(SD_MOSI, OUTPUT);
  pinMode(SD_CS, OUTPUT);
  pinMode(SD_MISO, INPUT_PULLUP);
  digitalWrite(SD_SCK, LOW);
  digitalWrite(SD_MOSI, HIGH);
  digitalWrite(SD_CS, HIGH);
  delay(5);

  uint8_t buf[10];

  // 1. Лінія в спокої: CS=HIGH, женемо такти. Картка не вибрана, тому має
  //    віддавати суцільні 0xFF (лінія підтягнута).
  for (int i = 0; i < 10; ++i) buf[i] = sdBitBangTransfer(0xFF);
  sdBitBangDump("CS=HIGH, 80 тактів:", buf, 10);

  // 2. CMD0 при CS=LOW.
  digitalWrite(SD_CS, LOW);
  delayMicroseconds(50);
  sdBitBangTransfer(0xFF);
  sdBitBangTransfer(0x40);  // CMD0
  sdBitBangTransfer(0x00);
  sdBitBangTransfer(0x00);
  sdBitBangTransfer(0x00);
  sdBitBangTransfer(0x00);
  sdBitBangTransfer(0x95);  // CRC для CMD0
  for (int i = 0; i < 10; ++i) buf[i] = sdBitBangTransfer(0xFF);
  sdBitBangDump("CMD0 відповідь:", buf, 10);
  digitalWrite(SD_CS, HIGH);
  sdBitBangTransfer(0xFF);

  // 3. Чи керуються лінії взагалі: пишемо рівень і читаємо пін назад.
  //    Якщо пін не піддається - він відданий іншій периферії або
  //    закорочений на платі.
  auto checkDrive = [](const char* name, uint8_t pin) {
    pinMode(pin, OUTPUT);
    digitalWrite(pin, LOW);
    delayMicroseconds(50);
    const bool low = digitalRead(pin);
    digitalWrite(pin, HIGH);
    delayMicroseconds(50);
    const bool high = digitalRead(pin);
    Logger::info("  %-5s (GPIO%-2d) керується: %s", name, pin,
                 (!low && high) ? "ТАК" : "НІ - пін не піддається!");
  };
  checkDrive("SCK", SD_SCK);
  checkDrive("MOSI", SD_MOSI);
  checkDrive("CS", SD_CS);

  // Аналіз відповіді на CMD0.
  bool allFF = true, allZero = true, sawR1 = false;
  for (int i = 0; i < 10; ++i) {
    if (buf[i] != 0xFF) allFF = false;
    if (buf[i] != 0x00) allZero = false;
    if (buf[i] != 0xFF && !(buf[i] & 0x80)) sawR1 = true;
  }

  Logger::info("------------------------------------------------------------");
  if (sawR1 && wasMounted) {
    Logger::info("ВИСНОВОК: картка ВІДПОВІЛА на bit-bang, і апаратний SPI до цього");
    Logger::info("  вже змонтував її. Тобто справні і залізо, і піни, і SPI -");
    Logger::info("  ця проба тут нічого не діагностує, лише демонтувала картку.");
  } else if (sawR1) {
    Logger::info("ВИСНОВОК: картка ВІДПОВІЛА на bit-bang, але апаратний SPI її не");
    Logger::info("  підняв. Залізо і піни справні - причину шукати в SPI");
    Logger::info("  (частота, спільна з дисплеєм шина, стан CS).");
  } else if (allFF) {
    Logger::info("ВИСНОВОК: суцільні FF - лінія підтягнута, але картка мовчить.");
    Logger::info("  Це поведінка порожнього слота або картки без живлення:");
    Logger::info("  перевір посадку картки в слоті та 3V3 на слоті.");
  } else if (allZero) {
    Logger::info("ВИСНОВОК: суцільні 00 - лінію тримає в нулі. Якщо при цьому");
    Logger::info("  MOSI/SCK/CS керуються, то MISO або не той пін, або закорочений.");
  } else {
    Logger::info("ВИСНОВОК: на лінії є активність, але це не R1. Найімовірніше");
    Logger::info("  збій синхронізації - але картка фізично присутня.");
  }

  // Повертаємо шину апаратному SPI, інакше дисплей залишиться без неї.
  SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  pinMode(SD_CS, OUTPUT);
  digitalWrite(SD_CS, HIGH);

  if (wasMounted) {
    Logger::warn("Картку демонтовано пробою - виконай reboot, щоб повернути SD.");
  }
  Logger::info("============================================================");
}
#endif  // !BOARD_ESP32_S3_LCD147
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
    YIELD_DISPLAY_BUS();
    // ОБОВ'ЯЗКОВА перевірка перед readRAW(): на відміну від cardType(), який
    // чесно віддає CARD_NONE при _pdrv == 0xFF, SDFS::readRAW() передає цей
    // самий 0xFF прямо в ff_sd_read(), а той робить s_cards[pdrv] БЕЗ
    // перевірки меж (масив на FF_VOLUMES елементів). Читання за межами
    // масиву + розіменування сміттєвого вказівника = миттєвий reset плати.
    // Саме так "status sd" на незмонтованій картці перезавантажував пристрій.
    #if defined(BOARD_ESP32_S3_LCD147)
    if (SD_MMC.cardType() == CARD_NONE) {
      Logger::warn("SD не змонтована - читати нічого (деталі: status sd+).");
    } else {
      SDCardInspector::printAll(SD_MMC, logger);
    }
    #else
    if (SD.cardType() == CARD_NONE) {
      Logger::warn("SD не змонтована - читати нічого (деталі: status sd+).");
    } else {
      SDCardInspector::printAll(SD, logger);
    }
    #endif
  } else if (section.equals("sd+")) {
    dumpSDInfo();
#endif
  } else {
    Logger::warn("use: status sys|cfg|sd|sd+|flash|flash+|littlefs");
  }
}

void setupSerialCommander() {
  commandHandler.registerCommand("status", "show device status state: status sys|cfg|sd|sd+|flash|flash+|littlefs",
                                 [](const String& args) { dumpStatus(args); });

  commandHandler.registerCommand("reboot", "reboot device (soft reset)", [](const String& args) {
    dispatcher.dispatch(EVT_REBOOT);
#if HAS_MQTT_CLIENT
    // EVT_REBOOT вище призводить до mqtt.disconnect("reboot"), але в
    // PicoMQTT-гілці це лише кладе publish у чергу вихідних команд - без
    // очікування offline-LWT не встигав піти до брокера до ESP.restart().
    mqtt.flushOutgoing(500);
#endif
#if defined(BOARD_ESP8266)
    Serial.println("[SystemReset] Rebooting...");
    Serial.flush();
    delay(100);
    ESP.restart();
#else
        SystemReset::reboot();
#endif
  });

  commandHandler.registerCommand("scan", "scan WiFi networks", [](const String& args) { WiFi_scan(); });

#if BOARD_HAS_SD && !defined(BOARD_ESP32_S3_LCD147)
  commandHandler.registerCommand(
      "sdprobe", "low-level TF card probe over SPI: sdprobe [force] (force demounts the card until reboot)",
      [](const String& args) { sdProbe(args.equalsIgnoreCase("force")); });

  commandHandler.registerCommand("sdscan", "brute-force SD_CS/SD_MISO pins (SCK/MOSI kept fixed)",
                                 [](const String& args) { sdScan(); });

  commandHandler.registerCommand("sdbb", "bit-bang TF card probe (no SPI peripheral), raw byte dump",
                                 [](const String& args) { sdBitBang(); });
#endif

  commandHandler.registerCommand("flip", "flip display (180)", [](const String& args) { display_flip(); });

#if BOARD_HAS_TOUCHSCREEN
  commandHandler.registerCommand("touchlog", "log touch coordinates at info level: touchlog on|off",
                                 [](const String& args) {
                                   if (args.equalsIgnoreCase("on")) {
                                     touchLogVerbose = true;
                                   } else if (args.equalsIgnoreCase("off")) {
                                     touchLogVerbose = false;
                                   } else {
                                     Logger::info("use: touchlog on|off");
                                     return;
                                   }
                                   Logger::info("touchlog = %s", touchLogVerbose ? "on" : "off");
                                 });
#endif

#if defined(I2C_SDA) && defined(I2C_SCL)
  commandHandler.registerCommand("i2cscan", "scan I2C bus and list device addresses",
                                 [](const String& args) { i2cScan(); });
#endif

#if BOARD_HAS_IMU
  commandHandler.registerCommand("imu", "show IMU orientation and raw Z acceleration",
                                 [](const String& args) {
                                   Logger::info("IMU: %s | X=%.2f Y=%.2f Z=%.2f g | вісь %s=%.2fg",
                                                ImuController::orientationName(ImuController::orientation()),
                                                ImuController::accelX(), ImuController::accelY(),
                                                ImuController::accelZ(), ImuController::upAxisName(),
                                                ImuController::upAxisValue());
                                 });
#endif

  commandHandler.registerCommand("led", "control led: led on|off", [](const String& args) {
    if (args.equalsIgnoreCase("on")) {
      Logger::info("LED ON");
    } else if (args.equalsIgnoreCase("off")) {
      Logger::info("LED OFF");
    } else {
      Logger::info("use: led on|off");
    }
  });

  commandHandler.registerCommand("clock", "show hide clock on screen: clock on|off", [](const String& args) {
    if (args.equalsIgnoreCase("on")) {
      show_clock(true);
    } else if (args.equalsIgnoreCase("off")) {
      show_clock(false);
    } else {
      Logger::info("use: clock on|off");
    }
  });

  commandHandler.registerCommand("brightness", "control screen brightness: brightness 0-100|auto", [](const String& args) {
    if (args.length() == 0) {
      Logger::info("use: brightness 0-100|auto");
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

#if defined(LITTLEFS_BACKGROUND_IMAGE) // blur <radius 1-8> [passes 1-3, default 1] 
  commandHandler.registerCommand(
      "blur", "blur background image: blur <radius 1-8> [passes 1-3, default 1]",
      [](const String& args) {
        if (args.length() == 0) {
          Logger::info("use: blur <radius 1-8> [passes 1-3, default 1]");
          return;
        }

        int spaceIdx = args.indexOf(' ');
        int radius = (spaceIdx < 0 ? args : args.substring(0, spaceIdx)).toInt();
        int passes = (spaceIdx < 0) ? 1 : args.substring(spaceIdx + 1).toInt();
        if (passes < 1) passes = 1;

        if (radius < 1 || radius > 8) {
          Logger::info("radius must be 1-8");
          return;
        }
        if (passes > 3) {
          Logger::info("passes clamped to 3 (heavier passes take long on-device)");
          passes = 3;
        }

        bool ok = ImageEffects::applyBoxBlur(spaceImage, (uint8_t)radius, (uint8_t)passes);
        Logger::info(ok ? "blur applied: radius=%d passes=%d" : "blur failed (image not loaded?)",
                     radius, passes);
      });
#endif

#if defined(LITTLEFS_BACKGROUND_IMAGE) // tint background image: tint <RRGGBB hex> [alpha 0.0-1.0, default 0.5]
  commandHandler.registerCommand(
      "tint", "tint background image: tint <RRGGBB hex> [alpha 0.0-1.0, default 0.5]",
      [](const String& args) {
        if (args.length() == 0) {
          Logger::info("use: tint <RRGGBB hex> [alpha 0.0-1.0, default 0.5]");
          return;
        }

        int spaceIdx = args.indexOf(' ');
        String hex = (spaceIdx < 0 ? args : args.substring(0, spaceIdx));
        float alpha = (spaceIdx < 0) ? 0.5f : args.substring(spaceIdx + 1).toFloat();

        if (hex.length() != 6) {
          Logger::info("color must be 6 hex chars, e.g. FF8800");
          return;
        }
        if (alpha < 0.0f) alpha = 0.0f;
        if (alpha > 1.0f) alpha = 1.0f;

        uint32_t rgb = strtoul(hex.c_str(), nullptr, 16);
        Pixel tint = Pixel::unpack(rgb);

        bool ok = ImageEffects::applyTint(spaceImage, tint, alpha);
        Logger::info(ok ? "tint applied: color=%s alpha=%.2f" : "tint failed (image not loaded?)",
                     hex.c_str(), alpha);
      });
#endif

#if defined(LITTLEFS_BACKGROUND_IMAGE) // contrast background image: contrast <factor 0.0-1.0>
  commandHandler.registerCommand(
      "contrast", "contrast background image: contrast <factor 0.0-1.0>",
      [](const String& args) {
        if (args.length() == 0) {
          Logger::info("use: contrast <factor 0.0-1.0>");
          return;
        }

        float factor = args.toFloat();

        bool ok = ImageEffects::applyContrast(spaceImage, factor);
        Logger::info(ok ? "contrast applied: factor=%f" : "contrast failed (image not loaded?)",
                     factor);
      });
#endif

#if defined(LITTLEFS_BACKGROUND_IMAGE) // sepia background image: sepia <amount 0.0-1.0>
  commandHandler.registerCommand(
      "sepia", "sepia background image: sepia <amount 0.0-1.0>",
      [](const String& args) {
        if (args.length() == 0) {
          Logger::info("use: sepia <amount 0.0-1.0>");
          return;
        }

        float amount = args.toFloat();

        bool ok = ImageEffects::applySepia(spaceImage, amount);
        Logger::info(ok ? "sepia applied: amount=%f" : "sepia failed (image not loaded?)",
                     amount);
      });
#endif

#if defined(LITTLEFS_BACKGROUND_IMAGE) // desaturate background image: desaturate <factor 0.0-1.0>
  commandHandler.registerCommand(
      "desaturate", "desaturate background image: desaturate <factor 0.0-1.0>",
      [](const String& args) {
        if (args.length() == 0) {
          Logger::info("use: desaturate <factor 0.0-1.0>");
          return;
        }

        float factor = args.toFloat();

        bool ok = ImageEffects::applyDesaturate(spaceImage, factor);
        Logger::info(ok ? "desaturate applied: factor=%f" : "desaturate failed (image not loaded?)",
                     factor);
      });
#endif

#if defined(LITTLEFS_BACKGROUND_IMAGE) // darken background image: darken <factor 0.0-1.0>
  commandHandler.registerCommand(
      "darken", "darken background image: darken <factor 0.0-1.0>",
      [](const String& args) {
        if (args.length() == 0) {
          Logger::info("use: darken <factor 0.0-1.0>");
          return;
        }

        float factor = args.toFloat();

        bool ok = ImageEffects::applyDarken(spaceImage, factor);
        Logger::info(ok ? "darken applied: factor=%f" : "darken failed (image not loaded?)",
                     factor);
      });
#endif

#if defined(LITTLEFS_BACKGROUND_IMAGE) // lighten background image: lighten <factor 0.0-1.0>
  commandHandler.registerCommand(
      "lighten", "lighten background image: lighten <factor 0.0-1.0>",
      [](const String& args) {
        if (args.length() == 0) {
          Logger::info("use: lighten <factor 0.0-1.0>");
          return;
        }

        float factor = args.toFloat();

        bool ok = ImageEffects::applyLighten(spaceImage, factor);
        Logger::info(ok ? "lighten applied: factor=%f" : "lighten failed (image not loaded?)",
                     factor);
      });
#endif

#if defined(LITTLEFS_BACKGROUND_IMAGE) // invert background image colors, no args
  commandHandler.registerCommand(
      "invert", "invert background image colors, no args",
      [](const String& args) {
        bool ok = ImageEffects::applyInvert(spaceImage);
        Logger::info(ok ? "invert applied" : "invert failed (image not loaded?)");
      });
#endif

#if defined(LITTLEFS_BACKGROUND_IMAGE) // threshold background image: threshold <level 0.0-1.0, default 0.5>
  commandHandler.registerCommand(
      "threshold", "threshold background image: threshold <level 0.0-1.0, default 0.5>",
      [](const String& args) {
        float threshold = (args.length() == 0) ? 0.5f : args.toFloat();

        bool ok = ImageEffects::applyThreshold(spaceImage, threshold);
        Logger::info(ok ? "threshold applied: level=%f" : "threshold failed (image not loaded?)",
                     threshold);
      });
#endif

#if defined(LITTLEFS_BACKGROUND_IMAGE) // rotate hue of background image: hue <angle degrees 0-360>
  commandHandler.registerCommand(
      "hue", "rotate hue of background image: hue <angle degrees 0-360>",
      [](const String& args) {
        if (args.length() == 0) {
          Logger::info("use: hue <angle degrees 0-360>");
          return;
        }

        float degrees = args.toFloat();
        float radians = degrees * (float)M_PI / 180.0f;

        bool ok = ImageEffects::applyHueRotate(spaceImage, radians);
        Logger::info(ok ? "hue applied: degrees=%f" : "hue failed (image not loaded?)", degrees);
      });
#endif

#if defined(LITTLEFS_BACKGROUND_IMAGE) // thermal-camera effect on background image, no args
  commandHandler.registerCommand(
      "thermal", "thermal-camera effect on background image, no args",
      [](const String& args) {
        bool ok = ImageEffects::applyThermal(spaceImage);
        Logger::info(ok ? "thermal applied" : "thermal failed (image not loaded?)");
      });
#endif

#if defined(LITTLEFS_BACKGROUND_IMAGE) // gamma-correct background image: gamma <value, e.g. 1.4>
  commandHandler.registerCommand(
      "gamma", "gamma-correct background image: gamma <value, e.g. 1.4>",
      [](const String& args) {
        if (args.length() == 0) {
          Logger::info("use: gamma <value, e.g. 1.4>");
          return;
        }

        float gamma = args.toFloat();
        if (gamma <= 0.0f) {
          Logger::info("gamma must be > 0.0");
          return;
        }

        bool ok = ImageEffects::applyGamma(spaceImage, gamma);
        Logger::info(ok ? "gamma applied: value=%f" : "gamma failed (image not loaded?)", gamma);
      });
#endif

#if defined(LITTLEFS_BACKGROUND_IMAGE) // posterize background image: posterize <levels, >=2>
  commandHandler.registerCommand(
      "posterize", "posterize background image: posterize <levels, >=2>",
      [](const String& args) {
        if (args.length() == 0) {
          Logger::info("use: posterize <levels, >=2>");
          return;
        }

        int levels = args.toInt();
        if (levels < 2) {
          Logger::info("levels must be >= 2");
          return;
        }

        bool ok = ImageEffects::applyPosterize(spaceImage, levels);
        Logger::info(ok ? "posterize applied: levels=%d" : "posterize failed (image not loaded?)",
                     levels);
      });
#endif

#if defined(LITTLEFS_BACKGROUND_IMAGE) // solarize background image: solarize <threshold 0.0-1.0, default 0.5>
  commandHandler.registerCommand(
      "solarize", "solarize background image: solarize <threshold 0.0-1.0, default 0.5>",
      [](const String& args) {
        float threshold = (args.length() == 0) ? 0.5f : args.toFloat();

        bool ok = ImageEffects::applySolarize(spaceImage, threshold);
        Logger::info(ok ? "solarize applied: threshold=%f" : "solarize failed (image not loaded?)",
                     threshold);
      });
#endif

#if defined(LITTLEFS_BACKGROUND_IMAGE) // duotone background image: duotone <dark RRGGBB> <light RRGGBB>
  commandHandler.registerCommand(
      "duotone", "duotone background image: duotone <dark RRGGBB> <light RRGGBB>",
      [](const String& args) {
        int spaceIdx = args.indexOf(' ');
        if (spaceIdx < 0) {
          Logger::info("use: duotone <dark RRGGBB> <light RRGGBB>");
          return;
        }

        String darkHex = args.substring(0, spaceIdx);
        String lightHex = args.substring(spaceIdx + 1);
        lightHex.trim();

        if (darkHex.length() != 6 || lightHex.length() != 6) {
          Logger::info("both colors must be 6 hex chars, e.g. duotone 1a0033 ffcc88");
          return;
        }

        Pixel dark = Pixel::unpack((uint32_t)strtoul(darkHex.c_str(), nullptr, 16));
        Pixel light = Pixel::unpack((uint32_t)strtoul(lightHex.c_str(), nullptr, 16));

        bool ok = ImageEffects::applyDuotone(spaceImage, dark, light);
        Logger::info(ok ? "duotone applied: dark=%s light=%s"
                        : "duotone failed (image not loaded?)",
                     darkHex.c_str(), lightHex.c_str());
      });
#endif

#if defined(LITTLEFS_BACKGROUND_IMAGE) // color-balance background image: balance <rMul> <gMul> <bMul>
  commandHandler.registerCommand(
      "balance", "color-balance background image: balance <rMul> <gMul> <bMul>",
      [](const String& args) {
        String rest = args;
        rest.trim();
        int i1 = rest.indexOf(' ');
        if (i1 < 0) {
          Logger::info("use: balance <rMul> <gMul> <bMul>");
          return;
        }
        String tok1 = rest.substring(0, i1);
        rest = rest.substring(i1 + 1);
        rest.trim();
        int i2 = rest.indexOf(' ');
        if (i2 < 0) {
          Logger::info("use: balance <rMul> <gMul> <bMul>");
          return;
        }
        String tok2 = rest.substring(0, i2);
        String tok3 = rest.substring(i2 + 1);

        float rMul = tok1.toFloat();
        float gMul = tok2.toFloat();
        float bMul = tok3.toFloat();

        bool ok = ImageEffects::applyColorBalance(spaceImage, rMul, gMul, bMul);
        Logger::info(ok ? "balance applied: r=%f g=%f b=%f" : "balance failed (image not loaded?)",
                     rMul, gMul, bMul);
      });
#endif

#if defined(LITTLEFS_BACKGROUND_IMAGE) // noise: add grain noise to background image: noise <amount 0.0-1.0>
  commandHandler.registerCommand(
      "noise", "noise: add grain noise to background image: noise <amount 0.0-1.0>",
      [](const String& args) {
        if (args.length() == 0) {
          Logger::info("use: noise <amount 0.0-1.0>");
          return;
        }

        float amount = args.toFloat();

        bool ok = ImageEffects::applyNoise(spaceImage, amount);
        Logger::info(ok ? "noise applied: amount=%f" : "noise failed (image not loaded?)", amount);
      });
#endif

#if defined(LITTLEFS_BACKGROUND_IMAGE) // vignette background image: vignette <strength 0.0-1.0>
  commandHandler.registerCommand(
      "vignette", "vignette background image: vignette <strength 0.0-1.0>",
      [](const String& args) {
        if (args.length() == 0) {
          Logger::info("use: vignette <strength 0.0-1.0>");
          return;
        }

        float strength = args.toFloat();

        bool ok = ImageEffects::applyVignette(spaceImage, strength);
        Logger::info(ok ? "vignette applied: strength=%f" : "vignette failed (image not loaded?)",
                     strength);
      });
#endif

#if defined(LITTLEFS_BACKGROUND_IMAGE) // pixelate background image: pixelate <blockSize, >=2>
  commandHandler.registerCommand(
      "pixelate", "pixelate background image: pixelate <blockSize, >=2>",
      [](const String& args) {
        if (args.length() == 0) {
          Logger::info("use: pixelate <blockSize, >=2>");
          return;
        }

        int blockSize = args.toInt();
        if (blockSize < 2 || blockSize > 255) {
          Logger::info("blockSize must be 2-255");
          return;
        }

        bool ok = ImageEffects::applyPixelate(spaceImage, (uint8_t)blockSize);
        Logger::info(ok ? "pixelate applied: blockSize=%d" : "pixelate failed (image not loaded?)",
                     blockSize);
      });
#endif

#if defined(LITTLEFS_BACKGROUND_IMAGE) // scanlines on background image: scanlines <darkenFactor 0.0-1.0>
  commandHandler.registerCommand(
      "scanlines", "scanlines on background image: scanlines <darkenFactor 0.0-1.0>",
      [](const String& args) {
        if (args.length() == 0) {
          Logger::info("use: scanlines <darkenFactor 0.0-1.0>");
          return;
        }

        float darkenFactor = args.toFloat();

        bool ok = ImageEffects::applyScanlines(spaceImage, darkenFactor);
        Logger::info(ok ? "scanlines applied: darkenFactor=%f"
                        : "scanlines failed (image not loaded?)",
                     darkenFactor);
      });
#endif

#if defined(LITTLEFS_BACKGROUND_IMAGE) // chromatic aberration on background image: chromatic <offsetPx, >=1>
  commandHandler.registerCommand(
      "chromatic", "chromatic aberration on background image: chromatic <offsetPx, >=1>",
      [](const String& args) {
        if (args.length() == 0) {
          Logger::info("use: chromatic <offsetPx, >=1>");
          return;
        }

        int offsetPx = args.toInt();
        if (offsetPx < 1 || offsetPx > 255) {
          Logger::info("offsetPx must be 1-255");
          return;
        }

        bool ok = ImageEffects::applyChromaticAberration(spaceImage, (uint8_t)offsetPx);
        Logger::info(ok ? "chromatic applied: offsetPx=%d"
                        : "chromatic failed (image not loaded?)",
                     offsetPx);
      });
#endif

#if defined(LITTLEFS_BACKGROUND_IMAGE) // edge detection (Sobel) on background image, no args
  commandHandler.registerCommand(
      "sobel", "edge detection (Sobel) on background image, no args",
      [](const String& args) {
        bool ok = ImageEffects::applySobelEdges(spaceImage);
        Logger::info(ok ? "sobel applied" : "sobel failed (image not loaded, or smaller than 3x3?)");
      });
#endif

#if defined(LITTLEFS_BACKGROUND_IMAGE) // emboss background image: emboss [strength, default 1.0]
  commandHandler.registerCommand(
      "emboss", "emboss background image: emboss [strength, default 1.0]",
      [](const String& args) {
        float strength = (args.length() == 0) ? 1.0f : args.toFloat();

        bool ok = ImageEffects::applyEmboss(spaceImage, strength);
        Logger::info(ok ? "emboss applied: strength=%f"
                        : "emboss failed (image not loaded, or smaller than 3x3?)",
                     strength);
      });
#endif

#if defined(LITTLEFS_BACKGROUND_IMAGE) // dither: ordered dithering (Bayer 8x8) on background image, no args
  commandHandler.registerCommand(
      "dither", "ordered dithering (Bayer 8x8) on background image, no args",
      [](const String& args) {
        bool ok = false;
        switch (spaceImage.colorDepth()) {
          case JpegColorDepth::RGB332:
            ok = ImageEffects::applyDitheringRGB332(spaceImage);
            break;
          case JpegColorDepth::RGB565:
            ok = ImageEffects::applyDitheringRGB565(spaceImage);
            break;
          case JpegColorDepth::RGB888:
            ok = ImageEffects::applyDitheringRGB888(spaceImage);
            break;
          default:
            Logger::info("dither: unsupported color depth (MONO1?)");
            return;
        }
        Logger::info(ok ? "dither applied" : "dither failed (image not loaded?)");
      });
#endif

#if defined(LITTLEFS_BACKGROUND_IMAGE) // background: load background image: background <LittleFS path>
  commandHandler.registerCommand(
      "background", "load background image: background <LittleFS path>",
      [](const String& args) {
        if (args.length() == 0) {
          Logger::info("use: background <LittleFS path>");
          return;
        }

        spaceImage.loadFromLittleFS(args.c_str());

      });
#endif

  commandHandler.registerCommand("dump-asuswrt", "test AsusWRT", [](const String& args) { testAsusWRT(); });

  Logger::info("SerialCommander setup done");
}

void setupBackgroundImage() {
#if defined(LITTLEFS_BACKGROUND_IMAGE)
  spaceImage.loadFromLittleFS(LITTLEFS_BACKGROUND_IMAGE,
                              SPRITE_COLOR_DEPTH > 8 ? JpegColorDepth::RGB565 : JpegColorDepth::RGB332); // 16 | 8
  setBackgroundImage(spaceImage);
  #if BOARD_4848S040
  ImageEffects::applyDesaturate(spaceImage, 0.3);
  ImageEffects::applyDarken(spaceImage, 0.25);
  #endif

  #if BOARD_ESP32_C6 || defined(BOARD_ESP32_C6_LCD096)
  ImageEffects::applyDarken(spaceImage, 0.15);
  #endif
  
  #if BOARD_ESP32_S3_LCD147
  ImageEffects::applyDesaturate(spaceImage, 0.3);
  // ImageEffects::applyBoxBlur(spaceImage, 2);
  ImageEffects::applyVignette(spaceImage, 0.3);
  ImageEffects::applyDarken(spaceImage, 0.3);
  #endif
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
  // _apply: на старті ми лише ЧИТАЄМО збережене значення, тому писати його
  // назад у NVS (як робив display_brightness()) не потрібно.
  display_brightness_apply(configStorage.getInt(CFG_DISPLAY_BRIGHTNESS, 100), isAutoBrightness);
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
      // _apply, а не display_brightness(): без запису в NVS на кожну зміну
      // показань сенсора (див. коментар біля display_brightness()).
      display_brightness_apply(lightSensor.value(), isAutoBrightness);
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
  #if BOARD_ESP32_C6_LCD096
  uint8_t space = 2;
  #else
  uint8_t space = 5;
  #endif
  // img.fillRect(0, 30, 320, 65, BG_COLOR);

  uint32_t freeHeap = ESP.getFreeHeap();
  uint32_t cpuFreq = ESP.getCpuFreqMHz();
  uint32_t uptimeSec = millis() / 1000;

  display.setTextSize(1);
  display.setTextFont(1);
  display.setTextColor(TFT_DARKGREY);

#if defined(ESP32)
  display.setCursor(space * 2, space * 2 + row++ * (space + display.fontHeight()));
  // %u + (unsigned): uint32_t на RISC-V (C6) це "long unsigned int", тобто під
  // %d він не підходить (varargs типи мусять збігатися). Каст робить рядок
  // однаковим і для Xtensa, і для RISC-V.
  display.printf(F("Uptime: %02u:%02u:%02u"), (unsigned)(uptimeSec / 3600),
                 (unsigned)((uptimeSec / 60) % 60), (unsigned)(uptimeSec % 60));
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

#elif BOARD_ESP32_C6_LCD096
  display.setCursor(space * 2, space * 2 + row++ * (space + display.fontHeight()));
  snprintf(buf, sizeof(buf), "CPU: %u MHz", (unsigned)cpuFreq);
  display.print(buf);

  display.setCursor(space * 2, space * 2 + row++ * (space + display.fontHeight()));
  snprintf(buf, sizeof(buf), "Loop rate: %u/s", (unsigned)display.loopFrameRate());
  display.print(buf);
#else
  display.setCursor(space * 2, space * 2 + row++ * (space + display.fontHeight()));
  snprintf(buf, sizeof(buf), "CPU: %u MHz   Loop rate: %u/s", (unsigned)cpuFreq,
           (unsigned)display.loopFrameRate());
  display.print(buf);
#endif


#if defined(ESP8266)
  // ESP8266 не має ESP.getHeapSize() - показуємо лише вільну пам'ять
  // display.setCursor(10, 10 + row++ * (5 + display.fontHeight()));
  // display.printf("Heap free: %d KB", freeHeap / 1024);
#else
  uint32_t totalHeap = ESP.getHeapSize();
  display.setCursor(space * 2, space * 2 + row++ * (space + display.fontHeight()));
  // %u + (unsigned) - див. коментар біля "Uptime" вище.
  // Порядок множення теж важливий: freeHeap * 100 для ~320 KB купи ще влазить
  // у 32 біти, але запас невеликий - рахуємо через 64-бітний проміжок.
  display.printf("Heap free: %u / %u (%u%%)", (unsigned)(freeHeap / 1024),
                 (unsigned)(totalHeap / 1024),
                 (unsigned)(totalHeap ? (uint64_t)freeHeap * 100 / totalHeap : 0));
#endif

#if defined(ESP32)
  char* dumpPingStr = dumpPingStatsStr();
  if (dumpPingStr) {
    display.setCursor(space * 2, space * 2 + row++ * (space + display.fontHeight()));
    display.print(dumpPingStr);  // було: повторний виклик dumpPingStatsStr()
  }

  #if BOARD_ESP32_C6_LCD096
  // Вузький екран (160px) - без відсотків, тому й getWiFiQuality() тут не
  // рахуємо (раніше передавався третім, зайвим аргументом на два %-специфікатори).
  snprintf(buf, sizeof(buf), "WiFi: %s (%d dBm)", WiFi.SSID().c_str(), WiFi.RSSI());
  #else
  snprintf(buf, sizeof(buf), "WiFi: %s (%d dBm / %d%%)", WiFi.SSID().c_str(), WiFi.RSSI(), getWiFiQuality(WiFi.RSSI()));
  #endif
  display.setCursor(space * 2, space * 2 + row++ * (space + display.fontHeight()));
  display.print(buf);


  snprintf(buf, sizeof(buf), "IP: %s", WiFi.localIP().toString().c_str());
  display.setCursor(space * 2, space * 2 + row++ * (space + display.fontHeight()));
  display.print(buf);

  snprintf(buf, sizeof(buf), "Brightness: %d%% %s", display.brightness(), isAutoBrightness ? "(auto)" : "");
  display.setCursor(space * 2, space * 2 + row++ * (space + display.fontHeight()));
  display.print(buf);

  #if LIGHT_SENSOR_PIN > 0
    // display.setTextSize(1);
    // display.setTextColor(TFT_DARKGREY);
    // display.setCursor(10, display.height() - 1 * (5 + display.fontHeight()));
    display.setCursor(space * 2, space * 2 + row++ * (space + display.fontHeight()));
    display.printf("LightSensor: %4d (%3d%%)", lightSensor.read(), lightSensor.value());
  #endif

  #if SCREEN_LOG_TAIL_LINES > 0
  display.setCursor(space * 2, space * 2 + row++ * (space + display.fontHeight()));
  display.println("------------------------------------");
  #if BOARD_TTGO_T1 || BOARD_ST7789
  int skip = 11; // hide logvele and tag
  #elif BOARD_ESP32_S3_LCD147
  int skip = 11; // hideloglevel only!
  #else
  int skip = 0;
  #endif

  ScreenLogTail& tail = screenLogTail();
  // Цикл іде від НАЙНОВІШОГО (count-1) до найстарішого (0) - найсвіжіший рядок
  // опиняється зверху. (Попередній коментар тут стверджував протилежне.)
  for (size_t i = tail.count(); i ;--i) {
    const char* logLine = tail.line(i - 1);
    // skip обрізає префікс "[I][tag ] " - але тільки якщо рядок реально
    // довший за нього. Інакше logLine + skip вказував би ЗА '\0', у застарілі
    // байти попереднього, довшого рядка того ж слота (сміття на екрані).
    display.println(strlen(logLine) > (size_t)skip ? logLine + skip : logLine);
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
    const char* msg = "TIME SYNC";
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

#if CLOCK_TEXT_FONT && CLOCK_TEXT_SIZE && CLOCK_POS_Y
  display.setTextFont(CLOCK_TEXT_FONT);
  display.setTextSize(CLOCK_TEXT_SIZE);
  #if !defined(CLOCK_POS_X)
  int x = (display.width() - display.textWidth(timeStr)) / 2;
  #else
  int x = CLOCK_POS_X;
  #endif

  display.setTextColor(TFT_CYAN);
  display.setCursor(x, CLOCK_POS_Y);
  display.print(timeStr);

  #if DATE_TEXT_FONT && DATE_TEXT_SIZE && DATE_POS_Y
  display.setTextFont(DATE_TEXT_FONT);
  display.setTextSize(DATE_TEXT_SIZE);

  char dateStr[16];
  ntp.ftime("%d.%m.%Y", dateStr, sizeof(dateStr));

  #if !defined(DATE_POS_X)
  int dateX = (display.width() - display.textWidth(dateStr)) / 2;
  #else
  int dateX = DATE_POS_X;
  #endif
  display.setTextColor(TFT_ORANGE);
  display.setCursor(dateX, DATE_POS_Y);
  display.print(dateStr);

  #endif

#elif BOARD_TTGO_T1 || BOARD_ESP32_S3_LCD147 || BOARD_ESP32_C6 || BOARD_ESP32_C6_LCD096
  // time
  #if BOARD_ESP32_C6
  display.setTextSize(5);
  #elif BOARD_ESP32_C6_LCD096
  display.setTextSize(2);
  #else
  display.setTextFont(7);  // великий "цифровий" шрифт (тільки цифри та ":")
  display.setTextSize(1);
  #endif

  int textW = display.textWidth(timeStr);
  int textH = display.fontHeight();
  int x = (display.width() - textW) / 2;
  #if BOARD_ESP32_C6
  int y = textH;
  #else
  int y = 30;
  #endif

  // display.getTextBound();
  // Затираємо попередній текст перед виводом нового
  // display.fillRect(0, y, display.width(), textH, TFT_BLACK);

  // display.setTextColor(TFT_DARKGREY);
  display.setTextColor(TFT_CYAN);
  display.setCursor(x, y);
  display.print(timeStr);

  // date
  char dateStr[16];
  ntp.ftime("%d.%m.%Y", dateStr, sizeof(dateStr));

  // Logger::info("font_height (clock) = %d", display.fontHeight()); // 40 (!)
  y += display.fontHeight() + 5;

  display.setTextFont(4);
  display.setTextSize(1);

  textW = display.textWidth(dateStr);
  x = (display.width() - textW) / 2;
  // Logger::info("font_height (date ) = %d", display.fontHeight()); // 28 (?)

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
#elif BOARD_4848S040 || BOARD_ST7789
  int16_t x = 0, y = 8;
  uint16_t textW, textH;
  // display.setTextFont(7); display.setTextSize(1); // великий "цифровий" шрифт (тільки цифри та ":")
  // display.setTextFont(6); display.setTextSize(1); // великий - красивий
  // display.setTextFont(4); display.setTextSize(1); // середній / так собі
  // display.setTextFont(2); display.setTextSize(2); // середній / не красиво взагалі
  display.setTextFont(1);  display.setTextSize(2); //  pretty nice
  // display.setTextColor(TFT_CYAN);
  // display.setTextColor(TFT_MAGENTA);
  display.setTextColor(TFT_DARKGREY);

  // display.getTextBounds(timeStr, 0, 0, &x1, &y1, &textW, &textH);
  textW = display.textWidth(timeStr);
  display.setCursor(display.width() - 10 - textW, y);
  display.print(timeStr);
  y += display.fontHeight();

  display.setTextFont(2);
  display.setTextSize(1);
  // display.setTextColor(TFT_ORANGE);
  ntp.ftime("%d.%m.%Y", timeStr, sizeof(timeStr));
  // display.setCursor(display.width() - 10 - textW + (textW - display.textWidth(timeStr)) / 2, y);
  display.setCursor(display.width() - 10 - display.textWidth(timeStr), y);
  display.print(timeStr);

  display.setTextFont(1);
  display.setTextSize(1);
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
  #elif defined(BOARD_ESP32_S3_LCD147)
    const int p[2] = {display.width() - 16, display.height() - 16};
  #elif defined(BOARD_ESP32_C6) || defined(BOARD_ESP32_C6_LCD096)
    const int p[2] = {display.width() - 16, display.height() - 16};
  #elif defined(BOARD_4848S040)
    const int p[2] = {display.width() - 16, display.height() - 16};
  #elif defined(BOARD_ST7789)
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

#if ESP32
void testRawTcpConnect() {
  static uint32_t lastTest = 0;
  if (millis() - lastTest < 5000) return;
  lastTest = millis();
  WiFiClient testClient;
  uint32_t t0 = millis();
  bool ok = testClient.connect("18.156.19.212", 1883, 5000); // 5с - щоб побачити реальний час, не зрізаний коротшим таймаутом
  uint32_t dt = millis() - t0;
  Logger::warn("raw TCP connect: %s, took %ums", ok ? "OK" : "FAIL", dt);
  if (ok) testClient.stop();
}
#else
void testRawTcpConnect() {
  Logger::error("I don't have WiFiClient");
}
#endif
void setup() {
  uint32_t freeHeap = ESP.getFreeHeap();
  setupSerial();
  Logger::info("free heap memory from scratch: %u", freeHeap);

  setupI2C();  // обов'язково ДО setupTouchScreen()/setupImu() - шина спільна

  setupSD();
  setupLittleFS();
  setupEventDispatcher();
  setupConfigStorage();
  setupSerialCommander();
  setupBlinkLED();
  setupDisplay();
  setupTouchScreen();
  setupImu();
  setupWiFi();
  setupNtpService();
  setupBackgroundImage();
  setupTaskCommander();
  setupLightSensor();
  setupMqttClient();
  setupFlipButton();
  setupWiFiIcon();
  loadConfig();

  // httpServer.setStaticSource(&littleFsSource);
  // // httpServer.setEventDispatcher(&dispatcher);
  // httpServer.begin();

  display.flush();
  // testAsusWRT();
  Logger::debug("free heap memory: %u", ESP.getFreeHeap());
  Logger::info("");
  Logger::info("> Ready. Enter 'list' for comand list.");
}

#if ESP32
#include <esp_wifi.h> // Обов'язково додайте цей системний заголовок
#endif
int wifi_state = 0;
void loop() {
  display.startWrite();
  PrintQueue::flush();
  doPing();
  drawBackgroundImage();
  drawSystemInfo();

  /* if (WiFi.status() != WL_CONNECTED) {
    return;
  } */

  /* #if ESP32
  // if (WiFi.status() == WL_CONNECTED) {
  if (wifi_state == 0 && WiFi.isConnected()) {
    Logger::info("WIFI connected");
    wifi_state = 1;

    // Вимикаємо Modem Sleep (WiFi.setSleep(WIFI_PS_NONE) для Arduino)
    esp_wifi_set_ps(WIFI_PS_NONE); 
    // Обмежуємо протокол до B/G/N (іноді AX/WiFi 6 викликає лаги на C6)
    esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N);

  } else if (wifi_state == 1 && !WiFi.isConnected()) {
    Logger::info("WIFI disconnected!");
    wifi_state = 0;
  }
  #endif */

  #if HAS_MQTT_CLIENT
  if (WiFi.isConnected()) {
    uint32_t t0 = millis();
    mqtt.loop();
    // testRawTcpConnect();   // <-- тимчасово замість mqtt.loop();
    uint32_t dt = millis() - t0;
    if (dt > 200) Logger::warn("mqtt.loop() took %ums", dt);
    // --- В loop(), замість (або поруч з) mqtt.loop() на час тесту: ---
  }
  #endif

  commandHandler.update();
  if (showClock) drawTime();

  // sendEmail();
  scheduler.loop();

  display.endWrite();

#if BOARD_HAS_TOUCHSCREEN
 touchController.update();
#endif
  updateImuFlip();

  display.flush();
  // loopRgbLed();

  delay(1);
}
