// main.cpp
//
// pio run -t compiledb
// esptool --port /dev/ttyUSB0 --after hard-reset chip-id
// python3 -m serial.tools.miniterm --echo --non-exclusive /dev/ttyUSB0 115200
// docker run --rm -it -p 1883:1883 -p 9883:9883 eclipse-mosquitto mosquitto -v -c /mosquitto/config/mosquitto.conf
// mosquitto_sub -h broker.hivemq.com -p 1883 -t "mykola-lavryk/#" -F "@Y-@m-@d @H:@M:@S [%q/%r] %-50t %p" # qos/retain
// mosquitto_pub -h 192.168.1.71 -p 1883 -t mykola-lavryk/command/mqtt-esp32-c6 -m "clock off"
// mosquitto_pub -h broker.hivemq.com -p 1883 -t mykola-lavryk/command/mqtt-esp32-c6-lcd096 -m "clock on"
// mosquitto_pub -h broker.hivemq.com -p 1883 -t mykola-lavryk/command/mqtt-esp32-c6 -m "clock on"
// mosquitto_pub -h broker.hivemq.com -p 1883 -t mykola-lavryk/command/mqtt-esp32-c6 -m "desaturate 0.30"
// mosquitto_pub -h broker.hivemq.com -p 1883 -t mykola-lavryk/command/mqtt-esp32-c6 -m "darken 0.30"
// mosquitto_pub -h broker.hivemq.com -p 1883 -t mykola-lavryk/command/mqtt-ttgo-t1 -m "clock on"
//
// ./esp bg-save assets/background-02-320x172.h   # 55040 значень, ~35 с
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
#if defined(ESP32) && __has_include(<soc/rtc_cntl_reg.h>)
#include <soc/rtc_cntl_reg.h>
// Регістр примусового download-boot для команди "bootloader".
//
// Перевіряти НАЯВНІСТЬ ЗАГОЛОВКА тут недостатньо: на класичному ESP32 (xtensa,
// ttgo-t1 / esp32-st7789) soc/rtc_cntl_reg.h є, але самого RTC_CNTL_OPTION1_REG
// у ньому немає - у того чипа download mode вмикається лише апаратно (GPIO0 на
// ресеті). Тому дивимось на сам макрос, інакше збірка падає з
// "'RTC_CNTL_OPTION1_REG' was not declared in this scope".
#if defined(RTC_CNTL_OPTION1_REG) && defined(RTC_CNTL_FORCE_DOWNLOAD_BOOT)
#define HAS_FORCE_DOWNLOAD_BOOT 1
#endif
#endif

#include "Display.h"
#if BOARD_HAS_SD
// SD_USE_SDMMC - внутрішній прапорець: SDMMC-режим є лише на платі з таким
// роз'ємом, і лише якщо його явно не відключили через SD_FORCE_SPI.
#if defined(BOARD_ESP32_S3_LCD147) && !defined(SD_FORCE_SPI)
#define SD_USE_SDMMC 1
#endif

#if defined(SD_USE_SDMMC)
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

#if defined(BOARD_ESP32_S3_LCD147)
// USB Mass Storage є лише на платі з native USB. Підключається незалежно від
// того, як читається картка (SPI чи SDMMC) - модуль отримує читач замиканням.
#include <SdMassStorage.h>
#endif
// Raw-читання секторів і розбір суперблока ext4 не залежать від того,
// SPI це чи SDMMC (доступ до картки - через шаблон), тому підключаються
// для обох гілок.
#include <Ext4SuperblockInspector.hpp>
#include <SDRawReader.hpp>
// Діагностика картки (sdbench/sdcrc/sdverify/sdmap) працює в обох режимах
// через спільний базовий клас SdBulkReader; різниця лише в тому, як сектори
// дістаються з заліза. ActiveBulkReader - псевдонім потрібної реалізації.
#if defined(SD_USE_SDMMC)
#include <SdMmcBulkReader.hpp>
using ActiveBulkReader = SdMmcBulkReader;
#else
#include <SdSpiBulkReader.hpp>
#include <SDImageServer.hpp>
using ActiveBulkReader = SdSpiBulkReader;
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
#include <LogLevelManager.hpp>
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

// HAS_ECOFLOW_CLIENT приходить з build_flags (див. platformio.ini, env з
// PicoMQTT). Свідомо НЕ виводимо його тут з __has_include: значення має бути
// однаковим і для компілятора, і для IDE-індексатора.
#if HAS_ECOFLOW_CLIENT
#include "Ecoflow/EcoflowClient.hpp"
#include "Ecoflow/EcoflowDeviceRegistry.hpp"
#endif

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
// runtime-override для ECOFLOW_AUTOCONNECT: "1"/"0"; порожнє -> build-time дефолт
const char* CFG_ECOFLOW_AUTOCONNECT = "ecoflow.auto";
// Останні випущені app-креденшели (команда 'ecoflow-login'). Зберігаються
// як резервна копія й журнал: застосувати їх з NVS на льоту не можна - MqttConfig
// копіює вказівники в конструкторі глобального EcoflowClient, тобто до setup().
const char* CFG_ECOFLOW_APP_ACCOUNT = "ecoflow.acc";
const char* CFG_ECOFLOW_APP_PASSWORD = "ecoflow.pass";
const char* CFG_ECOFLOW_APP_USER_ID = "ecoflow.uid";

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

#if HAS_ECOFLOW_CLIENT
EcoflowClient::Config makeEcoflowConfig() {
  EcoflowClient::Config config;
  config.mqttHost = ECOFLOW_MQTT_HOST;
  config.mqttPort = ECOFLOW_MQTT_PORT;
  config.mqttUsername = ECOFLOW_MQTT_USERNAME;
  config.mqttPassword = ECOFLOW_MQTT_PASSWORD;
  config.accessKey = ECOFLOW_ACCESS_KEY;
  config.secretKey = ECOFLOW_SECRET_KEY;
#if defined(ECOFLOW_USER_ID)
  // Потрібен лише для app-каналу (clientId ANDROID_..._<userId>).
  config.userId = ECOFLOW_USER_ID;
#endif
#if defined(ECOFLOW_LOGIN) && defined(ECOFLOW_PASSWORD)
  // Лише для 'ecoflow-login': перевипуск app-креденшелів.
  config.email = ECOFLOW_LOGIN;
  config.emailPassword = ECOFLOW_PASSWORD;
#endif
  // Окремий id від основного MQTT-клієнта: збіг id у межах акаунта змушує
  // брокер вибивати клієнтів по черзі.
  config.clientId = MQTT_CLIENT_ID "-ecoflow";
  return config;
}

EcoflowClient ecoflow(makeEcoflowConfig());
EcoflowDeviceRegistry ecoflowDevices;
#endif

LittleFsStaticSource littleFsSource(LittleFS);
HttpServer httpServer(HttpServerConfig{});
//NetworkSupervisor wifi;

// Хост і base64(login:password) приходять із secrets.ini через build_flags
// (ROUTER_HOST / ROUTER_LOGIN_AUTHORIZATION) - раніше вони були захардкожені
// тут, у файлі під git, попри те що механізм для секретів уже існував.
RouterApiClient routerApi(ROUTER_HOST, ROUTER_LOGIN_AUTHORIZATION);

// Обидва об'єкти визначені БЕЗУМОВНО, навіть коли BOARD_HAS_DISPLAY=0
// (env:esp32-c3). Причина: display.* і displayConfig зустрічаються в цьому
// файлі в сотнях місць, і обвішувати кожне "#if BOARD_HAS_DISPLAY" означало
// б розділити на дві гілки файл на 4000+ рядків. Замість цього на платі без
// дисплея сам Display стає порожнім: TFT_eSPI там - заглушка з
// include/Setup_Headless.h, усі методи inline й no-op, тому компілятор
// прибирає ці виклики цілком (у прошивці не лишається ні коду, ні буферів).
Display display;
TouchScreenConfig displayConfig = makeTouchScreenConfig();

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
  Logger::debug("Swipe FROM BOTTOM (e.g. open menu)");
}
void onSwipeFromTopHandler(TouchPoint start, TouchPoint end) {
  Logger::debug("Swipe FROM TOP (e.g. notification shade)");
}
void onSwipeFromLeftHandler(TouchPoint start, TouchPoint end) { Logger::debug("Swipe FROM LEFT (e.g. back)"); }
void onSwipeFromRightHandler(TouchPoint start, TouchPoint end) {
  Logger::debug("Swipe FROM RIGHT (e.g. side panel)");
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

// ---------------------------------------------------------------------------
// YIELD_DISPLAY_BUS() - RAII-дужка, яка тимчасово віддає SPI-шину дисплея.
//
// НАВІЩО: loop() тримає транзакцію дисплея відкритою через УВЕСЬ кадр
// (display.startWrite() ... display.endWrite()), і саме всередині неї
// викликаються commandHandler.update() та mqtt.loop(). Тобто будь-яка
// консольна чи MQTT-команда виконується з-під відкритої транзакції.
//
// А SPIClass::beginTransaction() бере НЕ рекурсивний мьютекс paramLock з
// portMAX_DELAY (framework-arduinoespressif32, libraries/SPI/src/SPI.cpp).
// Другий take з того самого потоку - вічний дедлок. Плата не просто "не
// відповідає": вона коректно блокується на семафорі, тому процесор
// віддано, idle task живий і ЖОДЕН watchdog її не перезавантажить.
//
// Хто саме бере транзакцію вдруге:
//   - драйвер SD (libraries/SD/src/sd_diskio.cpp, struct AcquireSPI) -
//     на КОЖНУ операцію з карткою;
//   - сам Arduino_GFX - наприклад Arduino_ST7735::setRotation() робить
//     _bus->beginWrite(), тобто навіть звичайний flip екрана з команди.
//
// Асиметрія викликів нижче навмисна:
//   endWrite()   - безпечно викликати зайвий раз: SPIClass::endTransaction()
//                  захищений прапорцем _inTransaction;
//   startWrite() - НЕ можна двічі поспіль: Arduino_TFT::startWrite() не має
//                  лічильника вкладеності і напряму робить beginWrite().
//
// Відновлення - УМОВНЕ, за display.isWriting(). Ті самі функції
// викликаються і з-під транзакції (консольний flip усередині кадру), і
// поза нею (updateImuFlip() - вже після endWrite()). Безумовне
// відновлення залишило б транзакцію відкритою там, де її не було, і
// наступний startWrite() у loop() дав би той самий дедлок.
// ---------------------------------------------------------------------------
#if defined(DISPLAY_BUS_YIELD) && DISPLAY_BUS_YIELD
struct DisplayBusYield {
  const bool _wasWriting;
  DisplayBusYield() : _wasWriting(display.isWriting()) {
    if (_wasWriting) tft.endWrite();
  }
  ~DisplayBusYield() {
    if (_wasWriting) tft.startWrite();
  }
};
#define YIELD_DISPLAY_BUS() DisplayBusYield _displayBusYield_
#else
#define YIELD_DISPLAY_BUS() ((void)0)
#endif

void display_flip() {
  // setRotation() усередині Arduino_GFX сам відкриває транзакцію шини -
  // без цієї дужки виклик з консольної команди (тобто з-під кадру) вішав
  // плату намертво, без шансу на watchdog.
  YIELD_DISPLAY_BUS();

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
  Logger::info(found ? "Devices found: %d" : "Nothing found - check pins/power", found);
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
  Logger::info("[IMU] orientation changed: %s (%s=%.2fg) -> flip",
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

#if HAS_ECOFLOW_CLIENT
// Дамп телеметрії EcoFlow у лог. Вимкнений за замовчуванням: пристрій шле
// quota кілька разів на секунду і десятками параметрів у кожному повідомленні.
bool ecoflowVerbose = false;

// Приймає або серійник, або короткий індекс зі списку (0..N) - набирати
// 16-символьний sn руками в консолі незручно. Порожній рядок = не розпізнано,
// причина вже в лозі.
static String ecoflowSerialFromKey(const String& key) {
  static TLogger _logger{"ecoflow"};
  String value = key;
  value.trim();
  if (value.length() == 0) {
    return String();
  }

  bool numeric = true;
  for (size_t i = 0; i < value.length(); i++) {
    if (!isdigit((int)value[i])) { numeric = false; break; }
  }

  if (numeric) {
    const size_t index = (size_t)value.toInt();
    if (index >= ecoflowDevices.devices().size()) {
      _logger.error("index %u out of range (0..%u)", index,
                    (unsigned)(ecoflowDevices.devices().size() - 1));
      return String();
    }
    return String(ecoflowDevices.devices()[index].info->serialNumber);
  }

  for (const auto& state : ecoflowDevices.devices()) {
    if (value == state.info->serialNumber) { return value; }
  }
  _logger.error("unknown device: %s", value.c_str());
  return String();
}

void setupEcoflow() {
  static TLogger _logger{"ecoflow"};

  #if defined(ESP32)
  ecoflow.onMqttConnect([](MqttTransportClient& client) {
    _logger.info("MQTT connected       [%s:%d]", client.host.c_str(), client.port);

    // Крок 1: Запит на миттєве оновлення (запуск потоку)
    // const char* payload = "{\"id\": 123456789, \"version\": \"1.0\", \"cmdFunc\": 254, \"cmdId\": 1, \"params\": {\"operateType\": \"latestQuotas\"}}";
    const char* payload = "{\"id\": 123456789, \"version\": \"1.0\", \"cmdCode\": \"latestQuotas\", \"params\": {}}";

    const size_t size = strlen(payload)+1;
    const auto ok = client.publish(
      "/app/device/property/R331ZEB4ZEBW0026",
      static_cast<const void*>(payload),
      size
    );
    if (!ok) {
      _logger.error("DELTA2 trigger fail!");
    }
  });

  ecoflow.onMqttDisconnect([](const MqttTransportClient& client) {
    _logger.info("MQTT disconnected    [%s:%d]", client.host.c_str(), client.port);
  });

  ecoflow.onMqttConnectionFail([](const MqttTransportClient& client) {
    _logger.info("MQTT connect fail    [%s:%d]", client.host.c_str(), client.port);
  });
  #endif

  // Зміна наявності мережі - головна подія, яку тут відслідковують: разом із
  // нею друкуємо, СКІЛЬКИ пристрій пробув у попередньому стані.
  ecoflowDevices.onGridChange([](const EcoflowDeviceState& state) {
    if (state.previousGrid == EcoflowGridState::Unknown) {
      // Перше визначення після старту - не перехід, тривалості ще немає.
      _logger.info("%s: %s (initial)", state.info->name, ecoflowGridStateName(state.grid));
      return;
    }
    _logger.warn("%s: %s -> %s after %s (change #%u)", state.info->name,
                 ecoflowGridStateName(state.previousGrid), ecoflowGridStateName(state.grid),
                 EcoflowDeviceRegistry::formatDuration(state.previousGridDurationMs).c_str(),
                 (unsigned)state.gridChangeCount);
  });

  ecoflowDevices.onSocChange([](const EcoflowDeviceState& state, int8_t previousSoc) {
    if (!ecoflowVerbose) { return; }
    _logger.info("%s: charge %d%% -> %d%%", state.info->name, static_cast<int>(previousSoc),
                 static_cast<int>(state.socPercent));
  });

  // Пристрій, від якого давно нічого не чути, вважаємо офлайн: /status
  // приходить не завжди, а тиша в quota - надійніший сигнал.
  scheduler.addCronTask(60 * 1000UL, []() { ecoflowDevices.expireStale(); });

  ecoflow.onQuota([](const String& serialNumber, JsonDocument& doc) {
    ecoflowDevices.applyQuota(serialNumber, doc);

    if (!ecoflowVerbose) { return; }

    // Схема повідомлення: {"id":..,"version":"1.0","timestamp":..,"params":{..}}
    // Набір ключів у params залежить від моделі, тому нічого не інтерпретуємо -
    // лише показуємо, що саме прийшло.
    JsonObjectConst params = doc["params"].as<JsonObjectConst>();
    const size_t count = params.isNull() ? 0 : params.size();
    _logger.info("quota %s params:%u", serialNumber.c_str(), static_cast<unsigned>(count));

    if (!ecoflowVerbose) { return; }

    for (JsonPairConst kv : params) {
      String value;
      serializeJson(kv.value(), value);
      _logger.debug("  %-32s = %s", kv.key().c_str(), value.c_str());
    }
  });

  // online/offline status update
  ecoflow.onStatus([](const String& serialNumber, JsonDocument& doc) {
    // {"id":..,"version":"1.0","timestamp":..,"params":{"status":0|1}}
    // Лог пише сам реєстр - він знає ім'я пристрою, а не лише sn.
    ecoflowDevices.applyStatus(serialNumber, doc);
  });

  // Стартуємо ОДРАЗУ: серійні номери прошиті, тому підписка не залежить ні від
  // REST, ні від синхронізованого часу (раніше старт доводилось відкладати саме
  // через підпис REST-запиту, що містить timestamp).
  //
  // ...але лише якщо auto-connect увімкнено: TLS-сесія коштує ~57 КБ heap, і на
  // платі без PSRAM це може бути дорожче за саму телеметрію.
  String autoConnectStored = configStorage.getString(CFG_ECOFLOW_AUTOCONNECT, "");
  const bool autoConnect = autoConnectStored.length() > 0 ? (autoConnectStored.toInt() != 0)
                                                          : (ECOFLOW_AUTOCONNECT != 0);
  if (autoConnect) {
    ecoflow.begin();
  } else {
    _logger.info("autoconnect is off - use 'ecoflow-start' to connect");
  }

  // Одноразова звірка з хмарою, коли зʼявиться час: REST потрібен лише щоб
  // помітити пристрій, доданий у застосунку, але відсутній у прошитому
  // переліку. Задача знімає себе після першої вдалої спроби.
  ecoflow.setRegistry(&ecoflowDevices);

  ecoflow.onAppCredentials([](const EcoflowMqttCredentials& credentials, const String& userId) {
    configStorage.setString(CFG_ECOFLOW_APP_ACCOUNT, credentials.certificateAccount);
    configStorage.setString(CFG_ECOFLOW_APP_PASSWORD, credentials.certificatePassword);
    configStorage.setString(CFG_ECOFLOW_APP_USER_ID, userId);
    _logger.info("saved to NVS");

    // Порівнюємо з тим, що зашито: якщо різне - на льоту не підхопиться.
    if (credentials.certificateAccount != String(ECOFLOW_MQTT_USERNAME)) {
      _logger.warn("build-time account differs (%s) - put the values above into "
                   "secrets.ini and reflash",
                   ECOFLOW_MQTT_USERNAME);
    }
  });

  // Щойно зʼявиться час (REST-підпис містить timestamp) - тягнемо ПОВНИЙ знімок
  // стану кожного пристрою. Без цього наявність мережі лишається unknown до
  // першої реальної зміни: MQTT-quota шле лише дельти.
  static TaskId ecoflowAuditTaskId = 0;
  ecoflowAuditTaskId = scheduler.addCronTask(30 * 1000UL, []() {
    if (!ntp.isSynced() || !WiFi.isConnected() || ecoflow.isBusy()) { return; }
    scheduler.removeTask(ecoflowAuditTaskId);
    ecoflow.syncSnapshotsAsync();
  });

  scheduler.addCronTask(60 * 1000UL, []() { commandHandler.execute("ecoflow"); });

  // command: ecoflow
  commandHandler.registerCommand("ecoflow", "show EcoFlow cloud MQTT status", [](const String args) {
    _logger.info("========== ECOFLOW ==========");
    _logger.info("connected = %s, account = %s", ecoflow.isConnected() ? "yes" : "no",
                 ecoflow.account().c_str());
    _logger.info("broker = %s:%d, channel = %s, verbose = %s", ECOFLOW_MQTT_HOST,
                 ECOFLOW_MQTT_PORT, EcoflowClient::channelName(ecoflow.channel()),
                 ecoflowVerbose ? "on" : "off");
    // TLS-сесія - найбільший споживач heap у цьому клієнті, тому цифри тут
    // корисніші за загальний 'dump-heap': саме вони кажуть, чи пройде REST.
    _logger.info("running = %s, heap = %u B free, largest block = %u B",
                 ecoflow.isRunning() ? "yes" : "no", (unsigned)ESP.getFreeHeap(),
                 (unsigned)ESP.getMaxAllocHeap());
    // Запас стеку мережевого таска: підстава змінювати MqttConfig::taskStackSize.
    _logger.info("net task stack headroom = %u B", (unsigned)ecoflow.networkStackHeadroom());
    if (ecoflow.lastError().length() > 0) {
      _logger.warn("last error: %s", ecoflow.lastError().c_str());
    }

    _logger.info("messages received = %u, last topic = %s", ecoflow.messageCount(),
                 ecoflow.lastTopic().length() > 0 ? ecoflow.lastTopic().c_str() : "(none)");

    const char* separator = "-------------------------------";

    _logger.info("");
    _logger.info("%-1s %-16s %-18s %-7s %6s %-9s %7s %9s", "#", "SERIAL", "NAME", "STATUS",
                 "CHARGE", "GRID", "LEFT", "AGE");
    _logger.info("%.1s %.16s %.18s %.7s %.6s %.9s %.7s %.9s", separator, separator, separator, separator,
                 separator, separator, separator, separator);
    size_t deviceIndex = 0;
    for (const auto& state : ecoflowDevices.devices()) {

      // Той самий індекс, що приймають 'ecoflow-params' і 'ecoflow-capture'.
      // CHARGE і TIME вирівняні вправо, щоб числа читались колонкою.
      // Три стани, а не два: REST-знімок заповнює CHARGE/TIME, але НЕ доводить,
      // що пристрій живий - раніше це виглядало як "offline із свіжими даними".
      // lastMessageMs == 0 означає саме "жодного повідомлення ще не чули".
      const char* presence = state.lastMessageMs == 0 ? "unknown"
                                                      : (state.online ? "online" : "offline");

      // Без ведучих пробілів: колонку вирівнює сам printf ("%6s").
      char charge[8] = "-";
      if (state.hasSoc()) { snprintf(charge, sizeof(charge), "%d%%", (int)state.socPercent); }

      // TIME стоїть ПІСЛЯ GRID: EcoFlow віддає одне поле remainTime і на заряд,
      // і на розряд, тому саме сусідство з GRID і пояснює, що воно означає.
      const String remaining = EcoflowDeviceRegistry::formatRemainTime(state.remainTimeMinutes);

      String gridFor = "-";
      if (state.gridSinceMs != 0) {
        gridFor = EcoflowDeviceRegistry::formatDuration(millis() - state.gridSinceMs);
      }
      //                1    2     3     4    5   6    7   8
      _logger.info("%-1u %-16s %-18s %-7s %6s %-9s %7s %9s",
                   (unsigned) deviceIndex++, // 1
                   state.info->serialNumber, // 2
                   state.info->name,         // 3
                   presence,
                   charge,
                   state.gridInferred
                    ? (String(ecoflowGridStateName(state.grid)) + "*").c_str()
                    : ecoflowGridStateName(state.grid),
                   remaining.c_str(),
                   gridFor.c_str());
    }
    _logger.info("");
  });

  // Обидві REST-команди йдуть у власний таск: TLS-хендшейк не вміщується
  // комфортно в стек головного loopTask, а пауза MQTT на час запиту заблокувала
  // б sketch loop() на кілька секунд (дисплей/тач завмирали б).
  // command: ecoflow-devices
  commandHandler.registerCommand(
    "ecoflow-devices", "fetch EcoFlow device list over REST (async, result in log)",
    [](const String args) {
      if (!ecoflow.refreshDevicesAsync()) {
        _logger.error("not started: %s", ecoflow.lastError().c_str());
        return;
      }
      _logger.info("request sent, result will appear in the log (MQTT suspended ~2-5 s)");
    }
  );

  // command: ecoflow-login
  commandHandler.registerCommand(
    "ecoflow-login", "issue private-API MQTT credentials (email+password -> account/password)",
    [](const String args) {
      if (!ecoflow.issueAppCredentialsAsync()) {
        _logger.error("not started: %s", ecoflow.lastError().c_str());
        return;
      }
      _logger.info("logging in, result will appear in the log (MQTT paused)");
    }
  );

  // command: ecoflow-capture
  commandHandler.registerCommand(
    "ecoflow-capture", "capture ALL params: ecoflow-capture <on|off> [sn|index|all]",
    [](const String args) {
      String rest = args;
      rest.trim();
      const int space = rest.indexOf(' ');
      String mode = space < 0 ? rest : rest.substring(0, space);
      String target = space < 0 ? String("all") : rest.substring(space + 1);
      mode.trim();
      target.trim();

      if (mode.length() == 0) {
        for (const auto& state : ecoflowDevices.devices()) {
          _logger.info("  %-16s capture=%-3s params=%u%s", state.info->serialNumber,
                       state.captureAll ? "all" : "imp",
                       (unsigned)state.trackedParams.size(),
                       state.droppedParams ? "  (limit reached)" : "");
        }
        _logger.info("use: ecoflow-capture <on|off> [sn|index|all]");
        return;
      }

      const bool enable = (mode == "on" || mode == "1" || mode == "true");
      // Порожній serial у setCaptureAll() означає "усі пристрої".
      String serial;
      if (target.length() > 0 && target != "all") {
        serial = ecoflowSerialFromKey(target);
        if (serial.length() == 0) { return; }
      }
      const size_t affected = ecoflowDevices.setCaptureAll(serial, enable);
      _logger.info("capture all = %s for %u device(s)", enable ? "on" : "off",
                   (unsigned)affected);
    }
  );

  // command: ecoflow-params
  commandHandler.registerCommand(
    "ecoflow-params", "show captured params: ecoflow-params <sn|index> [pattern, e.g. *_in_*]",
    [](const String args) {
      String rest = args;
      rest.trim();
      // Другий аргумент - glob-фільтр по ключу. Ключі зберігаються
      // нормалізованими (snake_case), тому '*_in_*' ловить і 'inv_ac_in_vol',
      // і 'ac_in_vol', попри різні схеми в API.
      String pattern;
      const int space = rest.indexOf(' ');
      if (space >= 0) {
        pattern = rest.substring(space + 1);
        pattern.trim();
        rest = rest.substring(0, space);
      }
      String key = rest;
      key.trim();
      if (key.length() == 0) {
        _logger.info("use: ecoflow-params <sn|index>");
        size_t i = 0;
        for (const auto& state : ecoflowDevices.devices()) {
          _logger.info("  %u  %-16s %-18s capture=%s params=%u", (unsigned)i++,
                       state.info->serialNumber, state.info->name,
                       state.captureAll ? "all" : "imp",
                       (unsigned)state.trackedParams.size());
        }
        return;
      }

      const String serial = ecoflowSerialFromKey(key);
      if (serial.length() == 0) { return; }

      // Друкуємо ЗАХОПЛЕНЕ, а не свіжий REST-запит: так команда миттєва і не
      // рве MQTT-сесію. Щоб підтягти повний стан з хмари - 'ecoflow-sync'.
      for (const auto& state : ecoflowDevices.devices()) {
        if (serial != state.info->serialNumber) { continue; }
        _logger.info("%s (%s): %u params, capture=%s%s", state.info->name,
                     state.info->serialNumber, (unsigned)state.trackedParams.size(),
                     state.captureAll ? "all" : "important-only",
                     state.snapshotAvailable ? "" : ", MQTT-only");
        if (state.droppedParams > 0) {
          _logger.warn("  %u more keys dropped - per-device limit reached",
                       (unsigned)state.droppedParams);
        }

        // std::map уже впорядкований лексикографічно, тому однакові префікси
        // ('inv_*', 'pd_*') виводяться згрупованими без окремого сортування.
        size_t shown = 0;
        for (const auto& kv : state.trackedParams) {
          if (pattern.length() > 0 &&
              !EcoflowDeviceRegistry::wildcardMatch(pattern.c_str(), kv.first.c_str())) {
            continue;
          }
          _logger.info("  %-34s = %.3f", kv.first.c_str(), kv.second);
          shown++;
        }
        if (pattern.length() > 0) {
          _logger.info("  %u of %u params match '%s'", (unsigned)shown,
                       (unsigned)state.trackedParams.size(), pattern.c_str());
        }

        // Порожній результат майже завжди означає одне з трьох - підказуємо, що
        // саме, бо інакше виглядає як "пристрій цього не шле".
        if (shown == 0 && pattern.length() > 0) {
          char normalized[EcoflowDeviceRegistry::kMaxKeyLength];
          EcoflowDeviceRegistry::normalizeKey(pattern.c_str(), normalized, sizeof(normalized));
          if (EcoflowDeviceRegistry::isBlacklistedParam(normalized)) {
            _logger.info("  reason: key is blacklisted");
          } else if (!state.captureAll &&
                     !EcoflowDeviceRegistry::isWhitelistedParam(normalized)) {
            _logger.info("  reason: not whitelisted; try 'ecoflow-capture on %s'",
                         state.info->serialNumber);
          } else if (pattern.indexOf('*') < 0) {
            _logger.info("  reason: not received yet (quota sends only changed fields)");
            _logger.info("  hint: keys are normalized - try '*%s*'", normalized);
          }
        }
        return;
      }
    }
  );

  // command: ecoflow-sync
  commandHandler.registerCommand(
    "ecoflow-sync", "pull full state snapshot for every device over REST",
    [](const String args) {
      if (!ecoflow.syncSnapshotsAsync()) {
        _logger.error("not started: %s", ecoflow.lastError().c_str());
        return;
      }
      _logger.info("snapshot sync started, result will appear in the log");
    }
  );

  // // command: ecoflow-start
  commandHandler.registerCommand(
    "ecoflow-start", "connect EcoFlow cloud MQTT (frees nothing, costs ~57 KB heap)",
    [](const String args) {
      if (!ecoflow.start()) {
        _logger.error("start failed: %s", ecoflow.lastError().c_str());
        return;
      }
      _logger.info("running = %s", ecoflow.isRunning() ? "yes" : "no");
    }
  );

  // command: ecoflow-stop
  commandHandler.registerCommand(
    "ecoflow-stop", "drop EcoFlow cloud MQTT and free its TLS session (~57 KB)",
    [](const String args) {
      if (!ecoflow.stop()) {
        _logger.error("stop failed: %s", ecoflow.lastError().c_str());
      }
    }
  );

  // command: ecoflow-auto
  commandHandler.registerCommand(
    "ecoflow-auto", "connect EcoFlow on boot: ecoflow-auto [on|off]",
    [](const String args) {
      String value = args;
      value.trim();
      if (value.length() == 0) {
        String stored = configStorage.getString(CFG_ECOFLOW_AUTOCONNECT, "");
        _logger.info("autoconnect = %s%s",
                     stored.length() > 0 ? (stored.toInt() ? "on" : "off")
                                         : (ECOFLOW_AUTOCONNECT ? "on" : "off"),
                     stored.length() > 0 ? "" : " (build-time default)");
        return;
      }
      const bool on = (value == "on" || value == "1" || value == "true");
      configStorage.setString(CFG_ECOFLOW_AUTOCONNECT, on ? "1" : "0");
      _logger.info("autoconnect = %s (applies on next boot)", on ? "on" : "off");
    }
  );

  // command: ecoflow-verbose
  commandHandler.registerCommand(
    "ecoflow-verbose", "toggle EcoFlow telemetry dump: ecoflow-verbose [on|off]",
    [](const String args) {
      String value = args;
      value.trim();
      if (value.length() > 0) {
        ecoflowVerbose = (value == "on" || value == "1" || value == "true");
      } else {
        ecoflowVerbose = !ecoflowVerbose;
      }
      _logger.info("verbose = %s", ecoflowVerbose ? "on" : "off");
    }
  );

  // command: ecoflow-cert
  commandHandler.registerCommand(
    "ecoflow-cert", "re-issue EcoFlow MQTT credentials over REST (async, result in log)",
    [](const String args) {
      if (!ecoflow.refreshCredentialsAsync()) {
        _logger.error("not started: %s", ecoflow.lastError().c_str());
        return;
      }
      _logger.info("request sent, result will appear in the log (MQTT suspended ~2-5 s)");
    }
  );
}
#endif

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

  #if !ESP8266
  mqtt.onConnect([](const MqttTransportClient& client) {
    _logger.info("MQTT connected       [%s:%d]", client.host.c_str(), client.port);
  });

  mqtt.onDisconnect([](const MqttTransportClient& client) {
    _logger.info("MQTT disconnected    [%s:%d]", client.host.c_str(), client.port);
  });

  mqtt.onConnectionFail([](const MqttTransportClient& client) {
    _logger.info("MQTT connect fail    [%s:%d]", client.host.c_str(), client.port);
  });
  #endif

  mqtt.begin();
  _logger.info("topic prefix = '%s'", mqtt.keyGenerator().prefix().c_str());

  // mqtt.publish(MQTT_LWT_TOPIC, "dummy-init-message", 1);
  scheduler.addCronTask(5 * 60 * 1000UL, []() { mqtt.publish(MQTT_LWT_TOPIC, "heartbeat"); });

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
// SD_USE_SDMMC - внутрішній прапорець: SDMMC-режим є лише на платі з таким
// роз'ємом, і лише якщо його явно не відключили через SD_FORCE_SPI.
#if defined(BOARD_ESP32_S3_LCD147) && !defined(SD_FORCE_SPI)
#define SD_USE_SDMMC 1
#endif

#if defined(SD_USE_SDMMC)
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
        Logger::info("SD card init done (%u Hz, attempt %d/%d)", (unsigned)freq, attempt, maxAttempts);
        return;
      }
      delay(100);
    }
    Logger::warn("SD init fail @ %u Hz", (unsigned)freq);
  }

  Logger::error("SD init fail. Check: card inserted? FAT32/FAT16 (NOT exFAT, NOT >32GB)? pins?");
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
  uint32_t uptimeSec = millis() / 1000;
  Logger::info("Uptime: %02u:%02u:%02u", (unsigned)(uptimeSec / 3600),
                 (unsigned)((uptimeSec / 60) % 60), (unsigned)(uptimeSec % 60));

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
// ACTIVE_SD — локальний (не глобальний!) макрос-псевдонім лише для функцій
// нижче: dumpSDlistDir()/dumpSDInfo()/dumpSdRaw()/dumpSdExt4(). Визначається безпосередньо
// перед використанням і одразу #undef-иться, щоб не впливати на інший код
// файлу чи транзитивні включення <SD.h> в сторонніх бібліотеках (напр.
// ESP Mail Client -> MB_FS.h), де глобальний "#define SD SD_MMC" ламає
// компіляцію (конфлікт з їхніми власними SD.*-викликами).
#if defined(SD_USE_SDMMC)
#define ACTIVE_SD SD_MMC
#include <SDCardInspector.hpp>
#else
#define ACTIVE_SD SD
#endif
 
void dumpSDlistDir(const char* dirname, uint8_t levels) {
  Logger::info("Directory contents: %s", dirname);
 
  File root = ACTIVE_SD.open(dirname);
  if (!root || !root.isDirectory()) {
    Logger::info("  (failed to open directory)");
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
    Logger::info("Card type: %s", "MMC");
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
  // uint64_t cardSize = ACTIVE_SD.cardSize() / (1024 * 1024);
  // Serial.printf(F("Розмір картки: %llu MB\n"), cardSize);
  Logger::info("Card size: %s", SizeFormatter::format(ACTIVE_SD.cardSize()));
  Logger::info("Used: %s (%.2f%%)", SizeFormatter::format(ACTIVE_SD.usedBytes()),
               ACTIVE_SD.usedBytes() * 100.0 / ACTIVE_SD.cardSize());
  Logger::info("Free:  %s (%.2f%%)", SizeFormatter::format(ACTIVE_SD.cardSize() - ACTIVE_SD.usedBytes()),
               (ACTIVE_SD.cardSize() - ACTIVE_SD.usedBytes()) * 100.0 / ACTIVE_SD.cardSize());
 
  Logger::info("============================================================");
}
 
// ---------------------------------------------------------------------------
// Порятунок даних з картки, яку не бачить хост (команди "sdraw" / "sdext4").
//
// НАВІЩО: коли SD картку не визначає комп'ютер, а ESP32 її читає, плата стає
// єдиним каналом доступу до даних. Обидві команди працюють в обхід файлової
// системи, тому не залежать від того, чи вміє ESP32 монтувати те, що на
// картці — ext4 (тип розділу 0x83) він не вміє в принципі.
//
// Спільна для обох перевірка cardType() != CARD_NONE — не косметика:
// SDFS::readRAW() на незмонтованій картці передає _pdrv == 0xFF прямо в
// ff_sd_read(), який індексує s_cards[pdrv] без перевірки меж, і плата
// ресетиться (та сама пастка, що описана вище для "status sd").
// ---------------------------------------------------------------------------

// Максимум секторів за один виклик "sdraw". Обмеження суто проти залиття
// логу: 16 секторів - це вже 512 рядків hexdump у Serial.
static constexpr uint32_t kSdRawMaxSectors = 16;

// Повертає перший LBA розділу за його індексом у MBR (1..4), або 0, якщо
// такого розділу немає. Дозволяє звати "sdext4 2" замість "sdext4 1056768".
static uint32_t sdPartitionFirstLba(uint8_t partitionIndex) {
  const auto partitions = SDCardInspector::collectPartitions(ACTIVE_SD);

  for (const auto& info : partitions) {
    if (info.index == partitionIndex) {
      return info.firstSectorLBA;
    }
  }

  return 0;
}

// Кількість секторів УСІЄЇ картки.
//
// НАВІЩО НЕ numSectors(): у SDFS (SPI-режим) він повертає розмір картки, а в
// SDMMCFS - розмір ЗМОНТОВАНОЇ файлової системи, тобто тут 512-мегабайтного
// boot-розділу (SD_MMC.cpp: numSectors() = totalBytes() / sector_size, а
// totalBytes() питає f_getfree про змонтований том). Через це на SD_MMC усе
// за межами першого розділу відкидалося як вихід за межі картки - разом з
// ext4-розділом, по який ми й прийшли.
//
// cardSize() в обох класах рахується з CSD-регістра самої картки, тому дає
// однаковий і правильний результат незалежно від режиму та від того, що саме
// змонтовано.
#if defined(BOARD_ESP32_S3_LCD147)
// Читач для USB Mass Storage. Окремий від sdImageBulkReader: той живе у
// гілці HTTP-сервера, якої на цій платі немає.
static ActiveBulkReader mscBulkReader;
#endif

static uint64_t activeCardSectors() {
  return ACTIVE_SD.cardSize() / SDRawReader::kSectorSize;
}

#if defined(BOARD_ESP32_S3_LCD147)
// Перемонтування картки на прохання USB-callback.
//
// Робиться з loop(), а не з самого callback: виклик end()/begin() драйвера з
// таску TinyUSB валив систему - плата перезавантажувалась посеред знімання
// образу, а хост бачив лише "No medium found". Тут ми у головному потоці,
// який і володіє драйвером картки.
//
// Навіщо взагалі: після серії CRC-збоїв ця картка перестає відповідати
// цілком, і без перемонтування знімання образу зупинилося б на першій такій
// серії - причому хост отримував би нулі, не дізнавшись про помилку.
void remountCardIfMscAsked() {
  if (!sdMassStorageNeedsRecovery()) {
    return;
  }

  YIELD_DISPLAY_BUS();
  Logger::warn("a run of read errors - remounting the card");

  ACTIVE_SD.end();
  delay(200);
  setupSD();

  mscBulkReader.begin(activeCardSectors());
  sdMassStorageInvalidatePrefetch();

  Logger::info("card remounted, cardType=%d", (int)ACTIVE_SD.cardType());
}
#endif


#if defined(BOARD_ESP32_S3_LCD147)
// "sdmsc on|off|status" - віддати картку хосту як USB-накопичувач.
//
// Читач секторів передається в MSC замиканням: сам модуль не знає, чи картка
// підключена по SPI, чи по SDMMC (див. коментар до SdMscSectorReader).
void dumpSdMsc(const String& args) {
  static TLogger logger("sdmsc");

  const String action = args.length() > 0 ? args : String("status");

  if (action.equalsIgnoreCase("on")) {
    YIELD_DISPLAY_BUS();

    if (ACTIVE_SD.cardType() == CARD_NONE) {
      logger.error("SD not mounted - nothing to serve to the host");
      return;
    }

    if (!mscBulkReader.isReady() && !mscBulkReader.begin(activeCardSectors())) {
      logger.error("card bulk reader failed to start");
      return;
    }

    sdMassStorageBegin(
        [](uint32_t lba, uint32_t count, uint8_t* out) {
          return mscBulkReader.readSectors(lba, count, out);
        },
        (uint32_t)activeCardSectors());
    return;
  }

  if (action.equalsIgnoreCase("off")) {
    sdMassStorageEnd();
    return;
  }

  sdMassStoragePrintStatus();
}
#endif  // BOARD_ESP32_S3_LCD147


// "sdraw <lba> [count]" - hexdump сирих секторів картки.
void dumpSdRaw(const String& args) {
  static TLogger logger("sdraw");

  YIELD_DISPLAY_BUS();

  if (ACTIVE_SD.cardType() == CARD_NONE) {
    logger.warn("SD not mounted - nothing to read (details: status sd+).");
    return;
  }

  char buffer[32];
  strlcpy(buffer, args.c_str(), sizeof(buffer));

  char* countToken = nullptr;
  char* lbaToken = strtok_r(buffer, " ", &countToken);

  if (lbaToken == nullptr || *lbaToken == '\0') {
    logger.warn("use: sdraw <lba> [count]   (e.g. sdraw 0, sdraw 1056768 2)");
    return;
  }

  const uint32_t lba = strtoul(lbaToken, nullptr, 0);  // 0 -> приймає і 0x-hex
  uint32_t count = (countToken != nullptr) ? strtoul(countToken, nullptr, 0) : 1;

  if (count == 0) {
    count = 1;
  }
  if (count > kSdRawMaxSectors) {
    logger.warn("count=%lu too large, clamped to %lu", (unsigned long)count,
                (unsigned long)kSdRawMaxSectors);
    count = kSdRawMaxSectors;
  }

  SDRawReader::hexdump(ACTIVE_SD, lba, count, logger);
}

// "sdext4 <partition> [sb_lba]" - розбір суперблока ext2/3/4 на розділі MBR.
//
// Другий аргумент - абсолютний LBA суперблока; потрібен, коли основний
// суперблок побитий і треба перевірити його резервну копію (адресу копії
// рахуємо з полів "per group" та "first block", які друкує ця ж команда).
void dumpSdExt4(const String& args) {
  static TLogger logger("ext4");

  YIELD_DISPLAY_BUS();

  if (ACTIVE_SD.cardType() == CARD_NONE) {
    logger.warn("SD not mounted - nothing to read (details: status sd+).");
    return;
  }

  char buffer[32];
  strlcpy(buffer, args.c_str(), sizeof(buffer));

  char* sbLbaToken = nullptr;
  char* partToken = strtok_r(buffer, " ", &sbLbaToken);

  if (partToken == nullptr || *partToken == '\0') {
    logger.warn("use: sdext4 <partition 1..4> [sb_lba]   (e.g. sdext4 2)");
    return;
  }

  const uint32_t partitionIndex = strtoul(partToken, nullptr, 0);
  if (partitionIndex < 1 || partitionIndex > 4) {
    logger.warn("partition number must be 1..4 (see status sd)");
    return;
  }

  const uint32_t partitionFirstLba = sdPartitionFirstLba((uint8_t)partitionIndex);
  if (partitionFirstLba == 0) {
    logger.warn("partition %lu not found in MBR (see status sd)", (unsigned long)partitionIndex);
    return;
  }

  // Суперблок лежить за фіксованим зміщенням 1024 байти від початку РОЗДІЛУ,
  // тобто через 2 сектори по 512 байт після його першого LBA.
  const bool hasExplicitLba = (sbLbaToken != nullptr && *sbLbaToken != '\0');
  const uint32_t superblockLba =
      hasExplicitLba ? strtoul(sbLbaToken, nullptr, 0)
                     : partitionFirstLba + Ext4SuperblockInspector::kSuperblockSectorOffset;

  logger.info("partition %lu: first LBA %lu, superblock at LBA %lu%s", (unsigned long)partitionIndex,
              (unsigned long)partitionFirstLba, (unsigned long)superblockLba,
              hasExplicitLba ? " (set manually)" : "");

  // 1024 байти суперблока = два послідовних сектори. Читаємо їх окремими
  // викликами readRAW() (його API - рівно один сектор за раз).
  uint8_t superblock[Ext4SuperblockInspector::kSuperblockSize];

  for (uint32_t i = 0; i < Ext4SuperblockInspector::kSuperblockSectorCount; ++i) {
    uint8_t* target = superblock + i * SDRawReader::kSectorSize;

    if (!SDRawReader::readSector(ACTIVE_SD, superblockLba + i, target)) {
      logger.error("failed to read LBA %lu (bad sector or out of range)",
                   (unsigned long)(superblockLba + i));
      return;
    }
  }

  Ext4SuperblockInspector::printAll(superblock, logger);
}

// "sdbench [lba] [sectors]" - фактична швидкість послідовного raw-читання.
//
// НАВІЩО: рішення "знімати образ через плату чи ні" залежить не від
// SD_FREQ у build_flags, а від виміряних КБ/с. Команда друкує ще й прогноз
// на 1 GiB - множенням на реальний обсяг даних одразу видно, скільки годин
// (чи днів) займе копіювання.
void dumpSdBench(const String& args) {
  static TLogger logger("sdbench");

  YIELD_DISPLAY_BUS();

  if (ACTIVE_SD.cardType() == CARD_NONE) {
    logger.warn("SD not mounted - nothing to read (details: status sd+).");
    return;
  }

  char buffer[32];
  strlcpy(buffer, args.c_str(), sizeof(buffer));

  char* rest = nullptr;
  char* lbaToken = strtok_r(buffer, " ", &rest);
  char* sectorsToken = strtok_r(nullptr, " ", &rest);
  char* chunkToken = strtok_r(nullptr, " ", &rest);

  const uint32_t lba = (lbaToken != nullptr && *lbaToken != '\0') ? strtoul(lbaToken, nullptr, 0) : 0;
  uint32_t sectors = (sectorsToken != nullptr) ? strtoul(sectorsToken, nullptr, 0) : 2048;
  const uint32_t chunk = (chunkToken != nullptr) ? strtoul(chunkToken, nullptr, 0) : 1;

  if (sectors == 0) {
    sectors = 2048;  // 1 MiB - достатньо, щоб усереднити накладні витрати
  }

  logger.info("reading %lu sectors (%lu KiB) from LBA %lu, in batches of %lu...", (unsigned long)sectors,
              (unsigned long)(sectors / 2), (unsigned long)lba, (unsigned long)chunk);

  RawReadStats stats;

  if (chunk > 1) {
    static ActiveBulkReader bulkReader;

    if (!bulkReader.isReady() && !bulkReader.begin(activeCardSectors())) {
      logger.error("failed to initialise the card bulk reader");
      return;
    }

    // Буфер під пачку виділяється в heap: при великому chunk впертись у
    // найбільший ВІЛЬНИЙ блок легко (LCD-буфери фрагментують купу), тому
    // друкуємо його поруч - інакше невдалий malloc виглядав би як "0 ok / 0 fail".
    logger.info("bulk mode: %lu B needed, largest free block %lu B",
                (unsigned long)(chunk * 512), (unsigned long)ESP.getMaxAllocHeap());
#if !defined(SD_USE_SDMMC)
    // pdrv є лише у SPI-реалізації: там він визначається перебором і його
    // значення - перше, що варто побачити в логу, якщо читання не пішло.
    logger.info("  pdrv=%u", (unsigned int)bulkReader.pdrv());
#endif

    stats = bulkReader.measureRead(lba, sectors, chunk);

    if (stats.sectorsOk == 0 && stats.sectorsFailed == 0) {
      logger.error("nothing was read - most likely not enough heap for the buffer");
      return;
    }
  } else {
    stats = SDRawReader::measureRead(ACTIVE_SD, lba, sectors);
  }

  logger.info("read       : %lu ok / %lu fail", (unsigned long)stats.sectorsOk,
              (unsigned long)stats.sectorsFailed);

  if (stats.sectorsFailed > 0) {
    logger.warn("first failed LBA: %lu", (unsigned long)stats.firstFailedLba);
  }

  if (stats.elapsedMs == 0 || stats.sectorsOk == 0) {
    logger.warn("nothing to measure (zero time or zero successful sectors)");
    return;
  }

  // Рахуємо в double: цілочисельне (sectorsOk * 512 * 1000) / elapsedMs
  // переповнює uint32 вже на кількох мегабайтах.
  const double bytes = (double)stats.sectorsOk * SDRawReader::kSectorSize;
  const double bytesPerSecond = bytes * 1000.0 / (double)stats.elapsedMs;
  const double minutesPerGiB = (1024.0 * 1024.0 * 1024.0) / bytesPerSecond / 60.0;

  logger.info("time       : %lu ms", (unsigned long)stats.elapsedMs);
  logger.info("speed      : %.1f KiB/s (%.2f MiB/s)", bytesPerSecond / 1024.0,
              bytesPerSecond / (1024.0 * 1024.0));
  logger.info("estimate   : %.1f min per 1 GiB (%.1f h per 50 GiB)", minutesPerGiB,
              minutesPerGiB * 50.0 / 60.0);
}

// "sdcrc <lba> <count> [chunk]" - CRC32 діапазону, прочитаного НА ПЛАТІ.
//
// НАВІЩО: два виклики з тими самими аргументами мусять дати той самий CRC.
// Якщо не дають - дані нестабільні, і команда одразу показує, на якому саме
// шляху: chunk=1 читає посекторно (CMD17), chunk>1 - пачками (CMD18).
// Без цієї команди нестабільність, помічену на хості, неможливо відрізнити
// від помилок транспорту.
void dumpSdCrc(const String& args) {
  static TLogger logger("sdcrc");

  YIELD_DISPLAY_BUS();

  if (ACTIVE_SD.cardType() == CARD_NONE) {
    logger.warn("SD not mounted - nothing to read (details: status sd+).");
    return;
  }

  char buffer[40];
  strlcpy(buffer, args.c_str(), sizeof(buffer));

  char* rest = nullptr;
  char* lbaToken = strtok_r(buffer, " ", &rest);
  char* countToken = strtok_r(nullptr, " ", &rest);
  char* chunkToken = strtok_r(nullptr, " ", &rest);

  if (lbaToken == nullptr || *lbaToken == '\0') {
    logger.warn("use: sdcrc <lba> <count> [chunk]   (e.g. sdcrc 1056768 1024 64)");
    return;
  }

  const uint32_t lba = strtoul(lbaToken, nullptr, 0);
  uint32_t count = (countToken != nullptr) ? strtoul(countToken, nullptr, 0) : 1024;
  const uint32_t chunk = (chunkToken != nullptr) ? strtoul(chunkToken, nullptr, 0) : 1;

  if (count == 0) {
    count = 1024;
  }

  static ActiveBulkReader crcReader;

  if (!crcReader.isReady() && !crcReader.begin(activeCardSectors())) {
    logger.error("failed to determine the card pdrv");
    return;
  }

  // Четвертий аргумент вмикає читання з голосуванням: два виклики команди з
  // тими самими аргументами мусять дати той самий CRC. Це і є перевірка, що
  // голосування справді прибирає мерехтіння бітів, а не просто маскує його.
  char* passesToken = strtok_r(nullptr, " ", &rest);
  const uint32_t passes = (passesToken != nullptr) ? strtoul(passesToken, nullptr, 0) : 0;

  if (passes >= 3) {
    const uint32_t chunkSectors = (chunk > SdBulkReader::kMaxVotedChunkSectors)
                                      ? SdBulkReader::kMaxVotedChunkSectors
                                      : (chunk > 0 ? chunk : 1);

    uint8_t* buffer = (uint8_t*)malloc(chunkSectors * SDRawReader::kSectorSize);
    if (buffer == nullptr) {
      logger.error("no heap for a %lu B buffer", (unsigned long)(chunkSectors * 512));
      return;
    }

    SdBulkReader::VoteStats total;
    uint32_t crcVoted = 0xFFFFFFFF;
    const uint32_t startMs = millis();

    for (uint32_t done = 0; done < count;) {
      const uint32_t remaining = count - done;
      const uint32_t take = (remaining < chunkSectors) ? remaining : chunkSectors;

      const auto voteStats = crcReader.readSectorsVoted(lba + done, take, buffer, (uint8_t)passes);

      total.sectorsStable += voteStats.sectorsStable;
      total.sectorsRecovered += voteStats.sectorsRecovered;
      total.sectorsUncertain += voteStats.sectorsUncertain;
      total.sectorsFailed += voteStats.sectorsFailed;
      total.bitsFixed += voteStats.bitsFixed;
      total.bitsUncertain += voteStats.bitsUncertain;

      crcVoted = SDRawReader::crc32Update(crcVoted, buffer, take * SDRawReader::kSectorSize);
      done += take;
    }

    crcVoted ^= 0xFFFFFFFF;
    free(buffer);

    logger.info("LBA %lu +%lu, majority vote over %lu passes -> CRC32 %08lX (%lu ms)",
                (unsigned long)lba, (unsigned long)count, (unsigned long)passes,
                (unsigned long)crcVoted, (unsigned long)(millis() - startMs));
    logger.info("  sectors: %lu stable / %lu recovered / %lu uncertain / %lu unreadable",
                (unsigned long)total.sectorsStable, (unsigned long)total.sectorsRecovered,
                (unsigned long)total.sectorsUncertain, (unsigned long)total.sectorsFailed);
    logger.info("  bits   : %lu fixed, %lu of them without a reliable majority",
                (unsigned long)total.bitsFixed, (unsigned long)total.bitsUncertain);
    return;
  }

  RawReadStats stats;
  const uint32_t crc = crcReader.crc32Range(lba, count, chunk, stats);

  logger.info("LBA %lu +%lu, chunk %lu -> CRC32 %08lX  (ok %lu / fail %lu, %lu ms)",
              (unsigned long)lba, (unsigned long)count, (unsigned long)chunk,
              (unsigned long)crc, (unsigned long)stats.sectorsOk,
              (unsigned long)stats.sectorsFailed, (unsigned long)stats.elapsedMs);
}

// "sdverify <lba> <sectors> [chunk] [passes] [delay_ms]" - чи повторюється читання.
//
// НАВІЩО: на цій картці SD-протокол помилок не показує (CRC16 кожного блоку
// валідний, fail=0), але дані щоразу різні. Команда читає діапазон кілька
// разів і друкує, які саме чанки не повторюються - тобто будує карту
// придатних до порятунку ділянок. Параметр delay_ms перевіряє, чи не зникає
// нестабільність при повільнішому читанні (просадка живлення / перегрів).
void dumpSdVerify(const String& args) {
  static TLogger logger("sdverify");

  YIELD_DISPLAY_BUS();

  if (ACTIVE_SD.cardType() == CARD_NONE) {
    logger.warn("SD not mounted - nothing to read (details: status sd+).");
    return;
  }

  char buffer[64];
  strlcpy(buffer, args.c_str(), sizeof(buffer));

  char* rest = nullptr;
  char* lbaToken = strtok_r(buffer, " ", &rest);
  char* sectorsToken = strtok_r(nullptr, " ", &rest);
  char* chunkToken = strtok_r(nullptr, " ", &rest);
  char* passesToken = strtok_r(nullptr, " ", &rest);
  char* delayToken = strtok_r(nullptr, " ", &rest);

  if (lbaToken == nullptr || *lbaToken == '\0') {
    logger.warn("use: sdverify <lba> <sectors> [chunk] [passes] [delay_ms]");
    return;
  }

  const uint32_t lba = strtoul(lbaToken, nullptr, 0);
  const uint32_t sectors = (sectorsToken != nullptr) ? strtoul(sectorsToken, nullptr, 0) : 1024;
  const uint32_t chunk = (chunkToken != nullptr) ? strtoul(chunkToken, nullptr, 0) : 64;
  const uint32_t passes = (passesToken != nullptr) ? strtoul(passesToken, nullptr, 0) : 2;
  const uint32_t delayMs = (delayToken != nullptr) ? strtoul(delayToken, nullptr, 0) : 0;

  static ActiveBulkReader verifyReader;

  if (!verifyReader.isReady() && !verifyReader.begin(activeCardSectors())) {
    logger.error("failed to determine the card pdrv");
    return;
  }

  logger.info("checking LBA %lu +%lu, chunk %lu, %lu passes, %lu ms pause",
              (unsigned long)lba, (unsigned long)sectors, (unsigned long)chunk,
              (unsigned long)passes, (unsigned long)delayMs);

  const auto stats = verifyReader.verifyRange(lba, sectors, chunk, (uint8_t)passes, delayMs, logger);

  logger.info("chunks     : %lu (stable %lu / unstable %lu)",
              (unsigned long)stats.chunksTotal, (unsigned long)stats.chunksStable,
              (unsigned long)stats.chunksUnstable);
  logger.info("unreadable: %lu sectors", (unsigned long)stats.sectorsFailed);
  logger.info("time       : %lu ms", (unsigned long)stats.elapsedMs);

  if (stats.chunksUnstable > 0) {
    logger.warn("first unstable LBA: %lu", (unsigned long)stats.firstUnstableLba);
  }
}

// "sdmap [first_lba] [last_lba] [points] [sectors] [passes]" - карта деградації.
//
// Друкує по символу на точку: '.' - читається повторювано, 'x' - щоразу
// інакше (мерехтіння бітів), 'E' - не читається зовсім. Потрібна, щоб
// відрізнити локальне пошкодження від деградації всієї картки: від цього
// залежить, чи має сенс витягувати дані і які саме ділянки.
void dumpSdMap(const String& args) {
  static TLogger logger("sdmap");

  YIELD_DISPLAY_BUS();

  if (ACTIVE_SD.cardType() == CARD_NONE) {
    logger.warn("SD not mounted - nothing to read (details: status sd+).");
    return;
  }

  char buffer[80];
  strlcpy(buffer, args.c_str(), sizeof(buffer));

  char* rest = nullptr;
  char* firstToken = strtok_r(buffer, " ", &rest);
  char* lastToken = strtok_r(nullptr, " ", &rest);
  char* pointsToken = strtok_r(nullptr, " ", &rest);
  char* sectorsToken = strtok_r(nullptr, " ", &rest);
  char* passesToken = strtok_r(nullptr, " ", &rest);

  const uint32_t totalSectors = activeCardSectors();

  const uint32_t firstLba = (firstToken != nullptr && *firstToken != '\0')
                                ? strtoul(firstToken, nullptr, 0) : 0;
  const uint32_t lastLba = (lastToken != nullptr) ? strtoul(lastToken, nullptr, 0) : totalSectors;
  const uint32_t points = (pointsToken != nullptr) ? strtoul(pointsToken, nullptr, 0) : 200;
  const uint32_t sectors = (sectorsToken != nullptr) ? strtoul(sectorsToken, nullptr, 0) : 8;
  const uint32_t passes = (passesToken != nullptr) ? strtoul(passesToken, nullptr, 0) : 3;

  static ActiveBulkReader mapReader;

  if (!mapReader.isReady() && !mapReader.begin(totalSectors)) {
    logger.error("failed to determine the card pdrv");
    return;
  }

  logger.info("map LBA %lu..%lu, %lu points of %lu sectors, %lu passes",
              (unsigned long)firstLba, (unsigned long)lastLba, (unsigned long)points,
              (unsigned long)sectors, (unsigned long)passes);
  logger.info("'.' repeatable read, 'x' flickers, 'E' unreadable");

  const uint32_t startMs = millis();
  const uint32_t unstable =
      mapReader.scanMap(firstLba, lastLba, points, sectors, (uint8_t)passes, logger);

  logger.info("unstable points: %lu of %lu (%.1f%%), %lu ms", (unsigned long)unstable,
              (unsigned long)points, points > 0 ? (100.0 * unstable / points) : 0.0,
              (unsigned long)(millis() - startMs));
}

#if !defined(SD_USE_SDMMC)
// ---------------------------------------------------------------------------
// Режим знімання образу картки (команда "sdimg").
//
// НАВІЩО ОКРЕМИЙ РЕЖИМ: картка і дисплей висять на СПІЛЬНІЙ SPI-шині
// (SD_SCK=1/SD_MOSI=2 і піни панелі), а віддача образу - це години
// безперервних читань по 32 KiB. Замість того, щоб синхронізувати кожну
// транзакцію з дисплеєм, у цьому режимі loop() просто НЕ малює нічого:
// шина повністю належить картці, а порядок доступу гарантований тим, що
// і читання, і віддача в сокет ідуть з одного потоку (див. loop()).
//
// Побічний ефект: поки режим active, екран показує останній кадр -
// статичну заставку з адресою сервера, яку малюємо один раз при вмиканні.
// ---------------------------------------------------------------------------

SDImageServer sdImageServer(SDImageServerConfig{});
static ActiveBulkReader sdImageBulkReader;

bool isSdImageModeActive() { return sdImageServer.isActive(); }

// Один раз малює заставку з адресою сервера - далі дисплей не чіпаємо.
static void drawSdImageSplash() {
  display.startWrite();
  display.clear(TFT_BLACK);
  display.setTextFont(2);
  display.setTextSize(1);
  display.drawText(6, 8, "SD IMAGE MODE", TFT_GREEN);
  display.drawText(6, 32, WiFi.localIP().toString().c_str(), TFT_WHITE);
  display.drawText(6, 56, "port 8080  /sd.img", TFT_WHITE);
  display.drawText(6, 88, "display frozen:", TFT_YELLOW);
  display.drawText(6, 110, "SPI belongs to card", TFT_YELLOW);
  display.endWrite();
  display.flush();
}

void dumpSdImage(const String& args) {
  static TLogger logger("sdimg");

  const String action = args.length() > 0 ? args : String("status");

  if (action.equalsIgnoreCase("on")) {
    if (sdImageServer.isActive()) {
      logger.warn("server is already running");
      return;
    }

    YIELD_DISPLAY_BUS();

    if (ACTIVE_SD.cardType() == CARD_NONE) {
      logger.error("SD not mounted - nothing to serve (details: status sd+).");
      return;
    }

    const size_t totalSectors = activeCardSectors();

    if (!sdImageBulkReader.isReady() && !sdImageBulkReader.begin(totalSectors)) {
      logger.error("failed to determine the card pdrv - cannot serve the image");
      return;
    }

    // Читач замикається на bulk-reader: пачками (CMD18) - утричі швидше за
    // посекторне читання, див. заміри в SdSpiBulkReader.hpp.
    sdImageServer.setSectorReader([](uint32_t lba, uint32_t count, uint8_t* out) {
      return sdImageBulkReader.readSectors(lba, count, out);
    });
    sdImageServer.setTotalSectors(totalSectors);

    if (!sdImageServer.begin()) {
      return;
    }

    // Вимикаємо WiFi power save НА ЧАС ЗНІМАННЯ.
    //
    // Це не мікрооптимізація, а різниця в два порядки: у режимі
    // WIFI_PS_MIN_MODEM (дефолт arduino-esp32) радіо просинається лише до
    // beacon-а, тому RTT дорівнює beacon interval (~100 мс), і потік по
    // TCP просідає до одиниць KiB/s - виміряно на цій платі: 7.2 KiB/s
    // проти 1.35 MiB/s, які дає сама картка. Для передачі десятків GiB
    // сон радіо неприйнятний; повертаємо його у "sdimg off".
    WiFi.setSleep(false);
    logger.info("WiFi power save disabled (RSSI %d dBm)", WiFi.RSSI());

    // Заставку малюємо ПІСЛЯ успішного підняття сервера: інакше при відмові
    // екран залишився б замороженим ні для чого.
    drawSdImageSplash();

    logger.info("display frozen, bus handed over to the card");
    logger.info("on the host: sudo qemu-nbd --read-only --connect=/dev/nbd0 \\");
    logger.info("  'json:{\"driver\":\"raw\",\"file\":{\"driver\":\"http\",\"url\":\"http://%s:8080/sd.img\"}}'",
                WiFi.localIP().toString().c_str());
    return;
  }

  if (action.equalsIgnoreCase("off")) {
    if (!sdImageServer.isActive()) {
      logger.warn("server is not running anyway");
      return;
    }

    sdImageServer.end();

    // Повертаємо енергозбереження радіо: у звичайному режимі плата не
    // ганяє гігабайти, а WIFI_PS_NONE тримає приймач увімкненим постійно.
    WiFi.setSleep(true);
    logger.info("display unfrozen, WiFi power save restored");
    return;
  }

  // status
  logger.info("server     : %s", sdImageServer.isActive() ? "active" : "stopped");

  if (sdImageServer.isActive()) {
    logger.info("адреса     : http://%s:8080/sd.img", WiFi.localIP().toString().c_str());
  }

  logger.info("image      : %llu bytes", (unsigned long long)sdImageServer.totalBytes());
  logger.info("served     : %llu bytes (requests %lu)",
              (unsigned long long)sdImageServer.bytesServed(),
              (unsigned long)sdImageServer.requestsServed());
  logger.info("failed     : %lu sectors (last LBA %lu)",
              (unsigned long)sdImageServer.badSectors(),
              (unsigned long)sdImageServer.lastBadLba());
}
#endif  // !SD_USE_SDMMC

#undef ACTIVE_SD

#if !defined(SD_USE_SDMMC)
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
  Logger::info("Pins: CS=%d SCK=%d MOSI=%d MISO=%d", SD_CS, SD_SCK, SD_MOSI, SD_MISO);

  if (SD.cardType() != CARD_NONE && !force) {
    Logger::info("The card is already mounted - a raw probe is unnecessary and unsafe.");
    Logger::info("  Card details: status sd");
    Logger::info("  If you need the probe right now: sdprobe force");
    Logger::info("  (this unmounts the card for good, until reboot).");
    Logger::info("============================================================");
    return;
  }

  // Звільняємо шину від драйвера SD (якщо він піднявся) і деактивуємо
  // дисплей - інакше ST7735 їстиме наші такти як свої команди.
  const bool wasMounted = (SD.cardType() != CARD_NONE);
  if (wasMounted) {
    Logger::warn("force: unmounting the card, a reboot will be needed after the probe");
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
  Logger::info("MISO=%d: external pull-up %s", SD_MISO,
               pulledExternally ? "PRESENT (card/resistor on the line)"
                                : "MISSING - most likely nothing is on this pin");

  SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  SPI.beginTransaction(SPISettings(400000, MSBFIRST, SPI_MODE0));

  // >=74 такти при CS=HIGH - обов'язковий перехід картки в SPI-режим.
  for (int i = 0; i < 10; ++i) SPI.transfer(0xFF);

  digitalWrite(SD_CS, LOW);
  uint8_t r1 = sdProbeCmd(0, 0x00000000, 0x95);  // CMD0 GO_IDLE_STATE
  Logger::info("CMD0 (GO_IDLE_STATE) -> R1=0x%02X %s", r1,
               r1 == 0x01   ? "OK (card is idle)"
               : r1 == 0xFF ? "NO RESPONSE - card/pins/power"
               : r1 == 0xFE ? "the DO line stays LOW (not connected or the card is busy)"
               : r1 == 0x00 ? "answered, but not idle"
                            : "unexpected");

  // CMD8 має сенс за будь-якої валідної R1 (біт 7 скинутий), не лише 0x01.
  if (!(r1 & 0x80)) {
    uint8_t r8 = sdProbeCmd(8, 0x000001AA, 0x87);  // CMD8 SEND_IF_COND
    uint8_t echo[4] = {0};
    for (int i = 0; i < 4; ++i) echo[i] = SPI.transfer(0xFF);
    Logger::info("CMD8 (SEND_IF_COND) -> R1=0x%02X echo=%02X %02X %02X %02X %s", r8, echo[0], echo[1],
                 echo[2], echo[3],
                 (r8 == 0x01 && echo[3] == 0xAA) ? "OK (SDHC/SDXC v2)"
                 : (r8 & 0x04)                   ? "illegal command (old SDSC v1)"
                                                 : "unexpected");
  }

  digitalWrite(SD_CS, HIGH);
  SPI.transfer(0xFF);
  SPI.endTransaction();

  Logger::info("------------------------------------------------------------");
  if (r1 == 0xFF) {
    Logger::info("VERDICT: the card does not respond at all - look for the cause in");
    Logger::info("  the pins (SD_CS/SD_SCK/SD_MOSI/SD_MISO in platformio.ini),");
    Logger::info("  the slot contacts or the 3V3 supply.");
  } else if (r1 == 0xFE) {
    Logger::info("VERDICT: the DO line always reads as 0. Two options:");
    Logger::info("  a) SD_MISO is the wrong pin / no card on it - if the line");
    Logger::info("     about the external pull-up above says MISSING, that is it;");
    Logger::info("  b) leftover busy after unmounting - a reboot helps then.");
    Logger::info("  Pick the pins automatically: sdscan");
  } else {
    Logger::info("VERDICT: bus and card are fine. If SD.begin() still");
    Logger::info("  fails, it is the filesystem: FAT32/FAT16 is required");
    Logger::info("  (the Arduino SD library mounts neither exFAT nor cards >32GB).");
  }
  if (wasMounted) {
    Logger::warn("The probe unmounted the card - run reboot to get SD back.");
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
    Logger::info("The card is already mounted on CS=%d MISO=%d - no scan needed.", SD_CS, SD_MISO);
    Logger::info("============================================================");
    return;
  }

  Logger::info("Fixed (shared with the display, hence already confirmed): SCK=%d MOSI=%d", SD_SCK, SD_MOSI);
  Logger::info("Current (being tested): CS=%d MISO=%d", SD_CS, SD_MISO);

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
        Logger::info("✅ FOUND: CS=%d MISO=%d -> CMD0 R1=0x01", cs, miso);
      } else if (!(r1 & 0x80)) {
        // Відповідь є, але не idle - теж вартий уваги кандидат.
        Logger::info("?  CS=%d MISO=%d -> CMD0 R1=0x%02X (answered, but not idle)", cs, miso, r1);
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
  Logger::info("Combinations tried: %d, found: %d", tried, found);
  if (found) {
    Logger::info("Put the found pair into platformio.ini (SD_CS/SD_MISO) and reflash.");
  } else {
    Logger::info("No pair responded. Most likely the card is not inserted,");
    Logger::info("  the slot is unpowered, or SD_SCK/SD_MOSI differ on this board,");
    Logger::info("  from the display pins (then a scan with fixed SCK/MOSI is useless).");
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
  Logger::info("Pins: CS=%d SCK=%d MOSI=%d MISO=%d (no SPI peripheral)", SD_CS, SD_SCK, SD_MOSI, SD_MISO);

  const bool wasMounted = (SD.cardType() != CARD_NONE);
  if (wasMounted) {
    Logger::warn("The card is mounted - unmounting; a reboot will be needed after the probe");
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
  sdBitBangDump("CS=HIGH, 80 clocks:", buf, 10);

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
  sdBitBangDump("CMD0 response:", buf, 10);
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
    Logger::info("  %-5s (GPIO%-2d) drivable: %s", name, pin,
                 (!low && high) ? "YES" : "NO - the pin cannot be driven!");
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
    Logger::info("VERDICT: the card ANSWERED bit-bang, and hardware SPI had already");
    Logger::info("  mounted it. So hardware, pins and SPI are all fine -");
    Logger::info("  this probe diagnoses nothing here, it only unmounted the card.");
  } else if (sawR1) {
    Logger::info("VERDICT: the card ANSWERED bit-bang, but hardware SPI did not");
    Logger::info("  bring it up. Hardware and pins are fine - look into SPI");
    Logger::info("  (clock rate, bus shared with the display, CS state).");
  } else if (allFF) {
    Logger::info("VERDICT: all FF - the line is pulled up, but the card is silent.");
    Logger::info("  This is how an empty slot or an unpowered card behaves:");
    Logger::info("  check that the card is seated properly and 3V3 is on the slot.");
  } else if (allZero) {
    Logger::info("VERDICT: all 00 - something holds the line low. If MOSI/SCK/CS");
    Logger::info("  can be driven, then MISO is either the wrong pin or shorted.");
  } else {
    Logger::info("VERDICT: there is activity on the line, but it is not R1. Most likely");
    Logger::info("  a sync glitch - but the card is physically present.");
  }

  // Повертаємо шину апаратному SPI, інакше дисплей залишиться без неї.
  SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  pinMode(SD_CS, OUTPUT);
  digitalWrite(SD_CS, HIGH);

  if (wasMounted) {
    Logger::warn("The probe unmounted the card - run reboot to get SD back.");
  }
  Logger::info("============================================================");
}
#endif  // !SD_USE_SDMMC
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
    #if defined(SD_USE_SDMMC)
    if (SD_MMC.cardType() == CARD_NONE) {
      Logger::warn("SD not mounted - nothing to read (details: status sd+).");
    } else {
      SDCardInspector::printAll(SD_MMC, logger);
    }
    #else
    if (SD.cardType() == CARD_NONE) {
      Logger::warn("SD not mounted - nothing to read (details: status sd+).");
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
  // Фрагментація важливіша за сам обсяг вільного heap: алокація падає, коли
  // немає ОДНОГО суцільного блоку потрібного розміру, навіть якщо сумарно
  // вільно вдесятеро більше. largest/free і є цим показником.
  commandHandler.registerCommand("heap", "show heap usage and fragmentation", [](const String args) {
    static TLogger _log{"heap"};
#if defined(ESP32)
    // heap_caps_* - ESP-IDF API, на ESP8266 його немає (див. #else).
    const size_t total   = heap_caps_get_total_size(MALLOC_CAP_8BIT);
    const size_t freeNow = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    const size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    const size_t minEver = heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT);

    _log.info("total        : %u B", (unsigned)total);
    _log.info("free         : %u B (%u%% of total)", (unsigned)freeNow,
              total ? (unsigned)(freeNow * 100 / total) : 0);
    _log.info("largest block: %u B", (unsigned)largest);
    _log.info("fragmentation: %u%%  (free minus largest = %u B in holes)",
              freeNow > 0 ? (unsigned)(100 - (largest * 100) / freeNow) : 0,
              (unsigned)(freeNow - largest));
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
    _log.info("uptime       : %lu s", (unsigned long)(millis() / 1000UL));
  });

#if defined(LITTLEFS_BACKGROUND_IMAGE)
  // Дамп ПОТОЧНОГО фону (тобто вже з накладеними ефектами: blur/desaturate/
  // darken із setupBackgroundImage() і команд) у вигляді C-хедера. Сенс саме в
  // ефектах: data/convert.c робить те саме з ОРИГІНАЛЬНОГО jpeg, тобто без них.
  //
  // Готовий хедер підключається через -D BACKGROUND_PROGMEM_HEADER (див.
  // platformio.ini) і тоді фон живе у Flash, а не в RAM - на 320x172 це
  // 110 КБ RAM, найбільший одиничний споживач на цій платі.
  //
  // Вивід іде напряму в Serial, без Logger: жодних префіксів, щоб файл можна
  // було зберегти байт-у-байт (див. ./esp bg-save).
  commandHandler.registerCommand(
    "bg-dump", "dump current background (with effects) as C header for assets/",
    [](const String args) {
      static TLogger _log{"bg"};
      if (!spaceImage.isLoaded()) {
        _log.error("background is not loaded");
        return;
      }
      if (spaceImage.colorDepth() != JpegColorDepth::RGB565) {
        _log.error("background is %d-bit, not RGB565 - rebuild with "
                   "SPRITE_COLOR_DEPTH=16 to dump full quality",
                   (int)spaceImage.colorDepth());
        return;
      }

      const uint16_t width = spaceImage.width();
      const uint16_t height = spaceImage.height();
      const uint16_t* pixels = spaceImage.bufferRGB565();
      if (pixels == nullptr) {
        _log.error("no RGB565 buffer");
        return;
      }

      // Глушимо логи на весь час дампу: інакше чужий рядок вклиниться ПОСЕРЕДИНІ
      // рядка з пікселями (MQTT сипле логи щосекунди, а дамп триває ~35 с) -
      // і фільтр по префіксу в './esp bg-save' такого вже не врятує.
      const LogLevel savedLevel = LogLevelManager::instance().getDefaultLevel();
      LogLevelManager::instance().setDefaultLevel(LogLevel::Error);

      Serial.printf("// generated by 'bg-dump' from %s (%ux%u, effects applied)\n",
                    LITTLEFS_BACKGROUND_IMAGE, (unsigned)width, (unsigned)height);
      Serial.println("#pragma once");
      Serial.println("#include <pgmspace.h>");
      Serial.println("#define HAS_BACKGROUND_PROGMEM_RGB565 1");
      Serial.printf("#define BACKGROUND_PROGMEM_WIDTH  %u\n", (unsigned)width);
      Serial.printf("#define BACKGROUND_PROGMEM_HEIGHT %u\n", (unsigned)height);
      // Розмір масиву - width*height ЕЛЕМЕНТІВ по 2 байти (не width*height*2:
      // це вдвічі більше, ніж потрібно).
      Serial.printf("const uint16_t background_progmem_rgb565[%uu * %uu] PROGMEM = {\n",
                    (unsigned)width, (unsigned)height);

      const uint32_t total = (uint32_t)width * height;
      for (uint32_t i = 0; i < total; i++) {
        Serial.printf("0x%04X,", pixels[i]);
        if ((i + 1) % 16 == 0) { Serial.println(); }
      }
      Serial.println();
      Serial.println("};");
      Serial.flush();

      LogLevelManager::instance().setDefaultLevel(savedLevel);
    }
  );
#endif

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

#if defined(HAS_FORCE_DOWNLOAD_BOOT)
  commandHandler.registerCommand(
      "bootloader", "reboot into ROM download mode (for flashing without BOOT/RESET buttons)",
      [](const String& args) {
        // НАВІЩО: на платах з native USB (S3 у режимі ARDUINO_USB_MODE=0)
        // esptool не може сам перевести плату в завантажувач - послідовність
        // DTR/RTS, якою він це робить через апаратний CDC, у TinyUSB не
        // відтворюється, і прошивка падає з "No serial data received".
        // Єдиною альтернативою лишалося тримати BOOT і тиснути RESET руками.
        //
        // Цей регістр - той самий шлях, яким користується сам ROM: прапорець
        // примусового download-boot зберігається в RTC-домені, тому переживає
        // перезапуск ядра.
        Logger::warn("rebooting into bootloader (download mode)");
        Serial.flush();
        delay(100);
        REG_WRITE(RTC_CNTL_OPTION1_REG, RTC_CNTL_FORCE_DOWNLOAD_BOOT);
        esp_restart();
      });
#endif

#if SCREEN_LOG_TAIL_LINES > 0
  commandHandler.registerCommand(
      "history", "print the buffered tail of the log (last SCREEN_LOG_TAIL_LINES lines)",
      [](const String& args) {
        ScreenLogTail& tail = screenLogTail();
        const size_t n = tail.count();

        if (n == 0) {
          Logger::info("history: buffer is empty");
          return;
        }

        // pause() обов'язковий: інакше рядки, які ми ЗАРАЗ друкуємо, самі
        // потраплять у той самий кільцевий буфер і витіснять з нього
        // справжню історію ще під час друку.
        tail.pause();

        // Пишемо напряму в Serial під тим самим rwlock, яким користується
        // логер, а не через Logger::info(): рядки в буфері ВЖЕ містять свій
        // префікс "[I][tag ] ", і друк через логер додав би поверх нього
        // другий.
        rwlock::write(Serial, 50, []() { Serial.println("=== log history (oldest first) ==="); });
        for (size_t i = 0; i < n; ++i) {
          const char* logLine = tail.line(i);
          rwlock::write(Serial, 50, [logLine]() { Serial.println(logLine); });
        }
        rwlock::write(Serial, 50, []() { Serial.println("=== end of history ==="); });

        tail.resume();
      });
#endif
#if BOARD_HAS_SD && !defined(SD_USE_SDMMC)
  commandHandler.registerCommand(
      "sdprobe", "low-level TF card probe over SPI: sdprobe [force] (force demounts the card until reboot)",
      [](const String& args) { sdProbe(args.equalsIgnoreCase("force")); });

  commandHandler.registerCommand("sdscan", "brute-force SD_CS/SD_MISO pins (SCK/MOSI kept fixed)",
                                 [](const String& args) { sdScan(); });

  commandHandler.registerCommand("sdbb", "bit-bang TF card probe (no SPI peripheral), raw byte dump",
                                 [](const String& args) { sdBitBang(); });
#endif

#if BOARD_HAS_SD
  commandHandler.registerCommand("sdraw", "hexdump raw SD sectors, bypassing the filesystem: sdraw <lba> [count]",
                                 [](const String& args) { dumpSdRaw(args); });

  commandHandler.registerCommand("sdext4", "parse ext2/3/4 superblock of an MBR partition: sdext4 <1..4> [sb_lba]",
                                 [](const String& args) { dumpSdExt4(args); });

  commandHandler.registerCommand("sdbench", "measure raw sequential read speed: sdbench [lba] [sectors] [chunk]",
                                 [](const String& args) { dumpSdBench(args); });

  commandHandler.registerCommand("sdcrc", "CRC32 of a sector range read on-device: sdcrc <lba> <count> [chunk] [vote_passes]",
                                 [](const String& args) { dumpSdCrc(args); });

  commandHandler.registerCommand(
      "sdverify", "check read repeatability, map unstable areas: sdverify <lba> <sectors> [chunk] [passes] [delay_ms]",
      [](const String& args) { dumpSdVerify(args); });

  commandHandler.registerCommand(
      "sdmap", "degradation map across the card: sdmap [first_lba] [last_lba] [points] [sectors] [passes]",
      [](const String& args) { dumpSdMap(args); });

#if !defined(SD_USE_SDMMC)
  commandHandler.registerCommand(
      "sdimg", "serve the whole card over HTTP for imaging (freezes the display): sdimg on|off|status",
      [](const String& args) { dumpSdImage(args); });
#endif

#if defined(BOARD_ESP32_S3_LCD147)
  commandHandler.registerCommand(
      "sdmsc", "expose the card to the host as a read-only USB drive: sdmsc on|off|status",
      [](const String& args) { dumpSdMsc(args); });
#endif
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
                                   Logger::info("IMU: %s | X=%.2f Y=%.2f Z=%.2f g | axis %s=%.2fg",
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
#if defined(BACKGROUND_PROGMEM_HEADER)
  // Фон запечений у Flash (див. BackgroundImages.cpp) - декодований буфер у RAM
  // не потрібен зовсім. Саме тут і економляться 110 КБ на 320x172 RGB565.
  //
  // Ціна: ефекти (blur/tint/desaturate) до такого фону не застосувати - він
  // read-only. Тому запікати треба ВЖЕ з ефектами: зібрати з
  // LITTLEFS_BACKGROUND_IMAGE і SPRITE_COLOR_DEPTH=16, підібрати вигляд
  // командами, потім 'bg-dump' (або ./esp bg-save assets/<file>.h).
  Logger::info("background: baked into flash, RAM buffer skipped");
#elif defined(LITTLEFS_BACKGROUND_IMAGE)
  spaceImage.loadFromLittleFS(LITTLEFS_BACKGROUND_IMAGE,
                              SPRITE_COLOR_DEPTH > 8 ? JpegColorDepth::RGB565 : JpegColorDepth::RGB332); // 16 | 8
  setBackgroundImage(spaceImage);
  #if BOARD_4848S040
  ImageEffects::applyDesaturate(spaceImage, 0.3);
  ImageEffects::applyDarken(spaceImage, 0.25);
  #endif

  #if BOARD_ESP32_C6 || defined(BOARD_ESP32_C6_LCD096)
  ImageEffects::applyDesaturate(spaceImage, 0.3);
  // ImageEffects::applyBoxBlur(spaceImage, 2);
  ImageEffects::applyDarken(spaceImage, 0.20);
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
  int skip = 11; // hide loglevel and tag
  #elif BOARD_ESP32_S3_LCD147
  int skip = 11; // hide loglevel only!
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
  if (!ntp.isSynced()) {
    const char* msg = "TIME SYNC";
    display.setTextSize(2);
    display.setTextColor(TFT_RED);
    display.setCursor(
      max(0, (int) (display.width() - display.textWidth(msg)) / 2),
      max(0, (int) (display.height() - display.fontHeight()) / 2)
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

#elif BOARD_TTGO_T1 || BOARD_ESP32_S3_LCD147
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
  int x = (display.width() - textW) / 2;
  #if BOARD_ESP32_C6
  int y = display.fontHeight();
  #else
  int y = 30;
  #endif

  // display.getTextBound();
  // Затираємо попередній текст перед виводом нового
  // display.fillRect(0, y, display.width(), display.fontHeight(), TFT_BLACK);

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
  uint16_t textW;
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

  commandHandler.registerCommand("blink", "LED control: blink on|off", [blinkLedTaskId](const String& args) {
    if (args.equalsIgnoreCase("on")) {
      scheduler.resume(blinkLedTaskId);
      configStorage.setBool(CFG_BLINK_LED, true);
      Logger::info("blink ON");
    } else if (args.equalsIgnoreCase("off")) {
      scheduler.pause(blinkLedTaskId);
      configStorage.setBool(CFG_BLINK_LED, false);
      Logger::info("blink OFF");
    } else {
      Logger::info("LED control: blink on|off");
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
#if HAS_ECOFLOW_CLIENT
  // Після setupNtpService(): REST-підпис EcoFlow використовує timestamp, а
  // MQTT-хендшейк - перевірку строку дії сертифіката.
  setupEcoflow();
#endif
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
#if defined(BOARD_ESP32_S3_LCD147)
  remountCardIfMscAsked();
#endif

#if BOARD_HAS_SD && !defined(SD_USE_SDMMC)
  // Режим знімання образу: дисплей навмисно не малюється - SPI-шина спільна
  // з карткою, і будь-яка транзакція панелі посеред читання сектора зіпсувала
  // б і кадр, і дані. Serial-команди обслуговуємо далі, щоб режим можна було
  // вимкнути ("sdimg off").
  if (isSdImageModeActive()) {
    sdImageServer.handleClient();
    commandHandler.update();
    delay(1);
    return;
  }
#endif

  display.startWrite();
  PrintQueue::flush();
  doPing();
  drawBackgroundImage();
  drawSystemInfo();

  #if HAS_MQTT_CLIENT
  if (WiFi.isConnected()) {
    uint32_t t0 = millis();
    mqtt.loop();
    // testRawTcpConnect();   // <-- тимчасово замість mqtt.loop();
    uint32_t dt = millis() - t0;
    if (dt > 200) {
      Logger::warn("mqtt.loop() took %ums", dt);
    }
    // --- В loop(), замість (або поруч з) mqtt.loop() на час тесту: ---
    #if HAS_ECOFLOW_CLIENT
    ecoflow.loop();
    #endif
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
