// main.cpp
//
// pio run -t compiledb
// esptool --port /dev/ttyUSB0 --after hard-reset chip-id
// python3 -m serial.tools.miniterm --echo --non-exclusive /dev/ttyUSB0 115200
// docker run --rm -it -p 1883:1883 -p 9883:9883 eclipse-mosquitto mosquitto -v -c /mosquitto/config/mosquitto.conf
// mosquitto_sub -h broker.hivemq.com -p 1883 -t "mykola-lavryk/#" -F "@Y-@m-@d @H:@M:@S [%q/%r] %-50t %p" # qos/retain
// mosquitto_pub -h 192.168.1.71 -p 1883 -t mykola-lavryk/command/mqtt-esp32-c6 -m "clock off"
// mosquitto_pub -h broker.hivemq.com -p 1883 -t mykola-lavryk/command/mqtt-esp32-c6-lcd096 -m "clock on"
// mosquitto_pub -h broker.hivemq.com -p 1883 -t mykola-lavryk/command/mqtt-esp32-c6-lcd096 -m "dino on"
// mosquitto_pub -h broker.hivemq.com -p 1883 -t mykola-lavryk/command/mqtt-esp32-c6-lcd096 -m "dino test"
// mosquitto_pub -h broker.hivemq.com -p 1883 -t mykola-lavryk/command/mqtt-esp32-c6 -m "clock on"
// mosquitto_pub -h broker.hivemq.com -p 1883 -t mykola-lavryk/command/mqtt-esp32-c6 -m "desaturate 0.30"
// mosquitto_pub -h broker.hivemq.com -p 1883 -t mykola-lavryk/command/mqtt-esp32-c6 -m "darken 0.30"
// mosquitto_pub -h broker.hivemq.com -p 1883 -t mykola-lavryk/command/mqtt-esp32-c3 -m "heap"
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
#include <algorithm>
#include <cstring>
#include <vector>
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

#include "Dino/DinoRenderer.hpp"  // без #if: LDF не обчислює препроцесор,
                                     // а lib/DinoGame має знайтись на всіх env
#include <Display.hpp>
#include <LittleFS.h>
#if defined(ESP32) && __has_include(<WiFiClientSecure.h>)
// Потрібен команді smtp-probe для TLS-режиму (перевірка mbedTLS ядра).
#include <WiFiClientSecure.h>
#endif
#include <TouchScreenConfig.h>
#if BOARD_HAS_IMU
#include <ImuController.h>
#endif

#include <AnalogSensor.hpp>
#include <CommandQueue.hpp>
#include <CommandResponse.hpp>
#include <ConfigStorage.hpp>
#include <ConsoleMqtt.hpp>
#include <EmailTarget.hpp>
#include <EspPartitionInspector.hpp>
#include <EventDispatcher.hpp>
#include <GmailSender.hpp>
#include <HttpServer.hpp>
#include <JpegImage.hpp>
#include <Logger.hpp>
#include <MqttClient.hpp>
#include <MqttKeyGenerator.hpp>
#include <MqttReplyTarget.hpp>
#include <NtpService.hpp>
#include <Journal.hpp>
#include <RwLock.hpp>
#include <SerialCommander.hpp>
#include <SystemReset.hpp>
#include <Watchdog.hpp>
#include <TaskController.hpp>
#include <Trace.hpp>
#include <NetworkSupervisor.hpp>
#include <RouterApiClient.hpp>
#include <RouterClientListParser.hpp>
#include <RouterClientListIterator.hpp>


// HAS_WEB_PORTAL приходить з build_flags (див. platformio.ini). Як і
// HAS_ECOFLOW_CLIENT, значення задається явно, а не виводиться з
// __has_include: воно має бути однаковим і для компілятора, і для
// IDE-індексатора.
#ifndef HAS_WEB_PORTAL
#define HAS_WEB_PORTAL 0
#endif

#if HAS_WEB_PORTAL
#include <WebCommandsModule.hpp>
#include <WebConsoleModule.hpp>
#include <WebFilesModule.hpp>
#include <WebNvsModule.hpp>
#include <WebPortal.hpp>
#include <WebSystemModule.hpp>
#include <WebWifiModule.hpp>
#if HAS_SCREEN_MIRROR
#include <WebScreenModule.hpp>
#endif
#if HAS_MQTT_CLIENT
#include <WebMqttModule.hpp>
#endif
#endif

// EcoflowClient/EcoflowDeviceRegistry/WebEcoflowModule - через App/AppGlobals.hpp.
#include "Ecoflow/EcoflowSetup.hpp"

#include "features.h"
#include "App/AppGlobals.hpp"
#include "BackgroundImages.hpp"
#include "Mqtt/Discovery.hpp"
#include "Mqtt/JsonApi.hpp"
#include "Screen/Background.hpp"
#include "Screen/DisplayBusYield.hpp"
#include "Sd/Sd.hpp"
#include "TestGfx.hpp"
#include "SizeFormatter.hpp"
#include "ntp.h"
#include "ping.h"
#include "setup.h"
#include "wifi.h"
#include "netcli.h"  // після wifi.h: netcli викликає WiFi_scan()
#include "WifiNetworks.hpp"
#include "journalcli.h"

#if BOARD_HAS_TOUCHSCREEN
#include <TouchController.h>
#endif

bool showClock = true;
bool isAutoBrightness = false;

// Єдиний listener у прошивці: перекладає події FSM у лог. Усе інше в коді
// питає стан у глобального WiFi (WiFi.isConnected() тощо) - воно працює
// однаково, хто б не викликав begin().
struct NetworkEventLogger : public INetworkSupervisorListener {
  const TLogger logger{"net"};

  void onConnecting(const WifiConnection& conn) override {
    logger.info("connecting to '%s'...", conn.ssid.c_str());
  }
  void onConnected(const WifiConnection& conn, const std::string& ip) override {
    logger.info("connected to '%s', IP %s (%d dBm)", conn.ssid.c_str(), ip.c_str(), WiFi.RSSI());
  }
  void onDisconnected(const std::string& ssid) override {
    logger.warn("disconnected from '%s'", ssid.c_str());
  }
  void onConnectionFailed(const WifiConnection& conn) override {
    logger.warn("failed to connect to '%s'", conn.ssid.c_str());
  }
  void onApStarted(const std::string& apSsid, const std::string& ip) override {
    logger.info("hotspot '%s' up at %s, still scanning for known networks", apSsid.c_str(),
                ip.c_str());
  }
  void onApStopped() override { logger.info("hotspot down"); }
};

NetworkEventLogger networkEventLogger;

// Хост і base64(login:password) приходять із secrets.ini через build_flags
// (ROUTER_HOST / ROUTER_LOGIN_AUTHORIZATION) - раніше вони були захардкожені
// тут, у файлі під git, попри те що механізм для секретів уже існував.
RouterApiClient routerApi(ROUTER_HOST, ROUTER_LOGIN_AUTHORIZATION);

#if HAS_DINO_GAME
DinoRenderer dinoRenderer;
#endif
bool dinoActive = false;
#if HAS_DINO_GAME
// Режим показу сітки спрайтів ("dino test"). Окремий режим, а не разовий
// кадр: разовий одразу затерся б наступною ітерацією loop().
bool dinoTestMode = false;
// Скільки ще смуг треба почистити після перемикання режиму. Кадр збирається
// за DISPLAY_SPLIT_COUNT проходів, тому одного clear() не досить.
uint8_t dinoPendingClear = 0;
#endif

// Тестова таблиця дисплея (команда "test-gfx") - реєструється поза #if,
// як dino/clock/flip: список команд однаковий на всіх платах.
bool testGfxActive = false;
TestGfxPattern testGfxPattern = TestGfxPattern::Bars;
// Скільки ще смуг треба почистити після перемикання режиму/патерну - той
// самий сенс, що dinoPendingClear (кадр збирається за splitCount() проходів).
uint8_t testGfxPendingClear = 0;
// "test-gfx on" без явного імені патерну - демо-режим: проходить усі патерни
// по черзі, поки "test-gfx <pattern>" не зафіксує один і не вимкне цикл.
bool testGfxAutoCycle = false;
uint32_t testGfxCycleTs = 0;
constexpr uint32_t kTestGfxCycleMs = 5000;

// SMTP-дим-тест: найкоротший шлях перевірити, що лист узагалі виходить із
// плати. Тіло листа - фіксований рядок, тому це перевірка саме транспорту, а
// не механізму захоплення виводу (для нього є команда "mailto").
//
// Раніше тут був власний виклик mailer.sendEmail() - тобто БЕЗ паузи MQTT,
// обов'язкової на C6, де дві TLS-сесії не влазять у heap. Тепер відправку
// робить той самий EmailTarget, що й "mailto": suspend/resume і логування
// heap лежать в одному місці, а не в двох копіях, що розходяться.
void sendEmail() {
#if HAS_GMAIL_SENDER
  static TLogger logger("sendmail");

  if (!WiFi.isConnected()) {
    logger.error("wifi is not connected");
    return;
  }

  // SMTP над TLS вимагає валідного часу для перевірки сертифіката. Без цієї
  // перевірки бібліотека сама полізе по NTP і заблокує таск ще на 10 с.
  if (!ntp.isSynced()) {
    logger.error("NTP not ready - valid system time required for TLS");
    return;
  }

  // Попередження ДО відправки, а не після: сесія блокує цей таск, тобто на
  // цей час стає і рендер кадру, і MQTT, і тач. Без цього рядка плата
  // виглядає завислою (саме так це й читалось у консолі).
  logger.warn("sending test mail to %s - blocks this task until the SMTP session ends",
              GMAIL_TEST_RECIPIENT);

  EmailTarget target(mailer, GMAIL_TEST_RECIPIENT, PIO_PIOENV ": smtp smoke test",
#if HAS_MQTT_CLIENT
                     &mqtt
#else
                     nullptr
#endif
  );
  static const char kBody[] = "SMTP smoke test from " PIO_PIOENV ".\n";
  target.deliver(kBody, sizeof(kBody) - 1, /*isFinal=*/true);
#else
  Logger::error("sendmail - GmailSender not found!!!");
#endif
}

#if HAS_MQTT_CLIENT
// Приймач відповідей на MQTT-команди. Один на пристрій, лінива ініціалізація
// (Meyer's singleton) - конструюється при першій команді, а не під час
// static-init, коли mqtt ще може бути не готовий.
//
// Топік БЕЗ префікса: його підставить MqttClient::resolveTopic() через
// MqttKeyGenerator, як і для будь-якого іншого топіка. Повний вигляд -
// "<prefix>/command/<client-id>/reply".
static std::shared_ptr<ResponseTarget> mqttReplyTarget() {
  static std::shared_ptr<ResponseTarget> target =
      std::make_shared<MqttReplyTarget>(mqtt, "command/" MQTT_CLIENT_ID "/reply");
  return target;
}
#endif

#if BOARD_HAS_LIGHT_SENSOR
AnalogSensor lightSensor(LIGHT_SENSOR_PIN, 0, 1855, 100, 0, 5);
#endif

#if BOARD_HAS_TOUCHSCREEN
// Увесь тач логується у verbose під власним тегом. Не в debug: типовий рівень
// DEFAULT_LOG_LEVEL=3 - це саме Debug (LogLevel, JournalEntry.hpp), тобто debug
// ішов у консоль і MQTT на кожну подію, а сирі координати з TouchEvents::update()
// - на кожне опитування, поки палець лежить. Щоб побачити дотики під час
// налагодження: 'journal level touch verbose'.
//
// Раніше цього не вміли, тому існувала окрема команда 'touchlog on|off' і
// глобальний прапорець, що піднімав рівень одного повідомлення до info. І
// команда, і прапорець зникли: керування рівнем за тегом тепер спільне.
static const TLogger touchLog{"touch"};

// Підписаний на onTouch (момент НАТИСКАННЯ), а не на onClick: для перевірки
// «чи взагалі бачить панель і чи не з'їхав мапер» потрібен кожен дотик, тоді
// як onClick мовчить, якщо жест виявився свайпом або переріс у hold - саме в
// тих випадках, коли причину й шукають.
void onTouchLog(TouchPoint p) { touchLog.verbose("Touch: %d, %d", p.x, p.y); }
void onHoldHandler(TouchPoint p, unsigned long ms) { touchLog.verbose("Hold at %d,%d for %lu ms", p.x, p.y, ms); }
void onDblClickHandler(TouchPoint p) { touchLog.verbose("Double click: %d, %d\n", p.x, p.y); }

void onSwipeLeftHandler(TouchPoint start, TouchPoint end) { touchLog.verbose("Swipe LEFT"); }
void onSwipeRightHandler(TouchPoint start, TouchPoint end) { touchLog.verbose("Swipe RIGHT"); }
void onSwipeUpHandler(TouchPoint start, TouchPoint end) { touchLog.verbose("Swipe UP"); }
void onSwipeDownHandler(TouchPoint start, TouchPoint end) { touchLog.verbose("Swipe DOWN"); }

void onSwipeFromBottomHandler(TouchPoint start, TouchPoint end) {
  touchLog.verbose("Swipe FROM BOTTOM (e.g. open menu)");
}
void onSwipeFromTopHandler(TouchPoint start, TouchPoint end) {
  touchLog.verbose("Swipe FROM TOP (e.g. notification shade)");
}
void onSwipeFromLeftHandler(TouchPoint start, TouchPoint end) { touchLog.verbose("Swipe FROM LEFT (e.g. back)"); }
void onSwipeFromRightHandler(TouchPoint start, TouchPoint end) {
  touchLog.verbose("Swipe FROM RIGHT (e.g. side panel)");
}

void onHoldDrawPoints(TouchPoint p, unsigned long ms) {
  // У грі утримання - це високий стрибок, а не запит debug-рамки: інакше
  // кожен такий стрибок залишав би на екрані жовті кола на 10 секунд.
  if (dinoActive) return;
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

void dumpAsusClientList(String& json) {
  std::vector<RouterClientInfo> clients;
  if (!RouterClientListParser::parse(json, clients)) {
    Logger::error("can't parse client list json. [%d]", clients.capacity());
  }

  RouterClientListIterator it(std::move(clients));
  while (it.hasNext()) {
    const RouterClientInfo& c = it.next();
    Logger::info("client=%-30s timer=%9s", c.name.c_str(), c.timer.c_str());
  }
}

void testAsusWRT() {
  Logger::info("====== AsusWRT test script =======");
  Logger::info("free heap: %u", ESP.getFreeHeap());
  if (!routerApi.login()) {
    Logger::error("AsusWRT login fail");
    return;
  }
  String json;
  if (!routerApi.fetchClientListJson(json)) {
    Logger::error("AsusWRT fetch client fail");
  }
  dumpAsusClientList(json);
  Logger::info("------ AsusWRT test script -------");
  Logger::info("");
}

// Офлайн-варіант testAsusWRT(): той самий розбір, але JSON береться з
// LittleFS, а не з роутера. Потрібен, коли роутер недоступний - перевірити,
// що парсер ще розуміє формат get_clientlist (зразок кладеться uploadfs).
void testAsusWRT2() {
  Logger::info("====== AsusWRT test script =======");
  Logger::info("free heap: %u", ESP.getFreeHeap());

  const char* path = "/asus-get_clientlist.json";
  File file = LittleFS.open(path, "r");
  if (!file || file.isDirectory()) {
    Logger::error("Can't open file (%s)", path);
    return;
  }

  String json = file.readString();
  file.close();
  dumpAsusClientList(json);
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

// Вмикає/вимикає ігровий режим. Гра НЕ малюється поверх звичайного екрана -
// вона його заміщає (див. loop()), тому перемикач тут же чистить кадр: інакше
// на платах, де height() не ділиться на DISPLAY_SPLIT_COUNT рівно, останні
// рядки старої картинки лишились би на екрані назавжди.
void dino_set_active(bool on) {
#if HAS_DINO_GAME
  if (on && !dinoRenderer.ready()) {
    Logger::warn("dino: renderer not ready");
    return;
  }

  dinoActive = on;

  if (on) {
    dinoRenderer.game().reset();
  } else if (dinoRenderer.game().highScoreDirty()) {
    // Рекорд міг лишитись незбереженим, якщо гру вимкнули раніше, ніж
    // відпрацював cron-таск (див. setupDinoGame()).
    configStorage.setInt(CFG_DINO_HIGHSCORE, (int32_t)dinoRenderer.game().highScore());
    dinoRenderer.game().clearHighScoreDirty();
  }

  // Малювати ЗВІДСИ не можна. Команда виконується з commandHandler.update(),
  // тобто вже всередині транзакції кадру, а Arduino_HWSPI::beginWrite() на
  // спільній шині (обидві C6-плати) робить SPI.beginTransaction() БЕЗ обліку
  // вкладеності. Другий захід у той самий нерекурсивний мьютекс вішає плату
  // намертво, і watchdog не рятує - та сама пастка, що описана в
  // docs/architecture.md про YIELD_DISPLAY_BUS. Тому тут лише прапорець,
  // а чистить екран loop() у своїй транзакції.
  dinoTestMode = false;
  dinoPendingClear = display.splitCount();

  Logger::info("dino game %s", on ? "ON" : "OFF");
#else
  (void)on;
  Logger::info("dino: display game not available on this board");
#endif
}

// Вмикає/вимикає тестову таблицю, лишаючи патерн і testGfxAutoCycle як є.
// Той самий прийом, що dino_set_active(): лише прапорці, малює loop() у своїй
// транзакції шини (див. коментар там же про SPI.beginTransaction() без обліку
// вкладеності).
void testgfx_set_active(bool on) {
  testGfxActive = on;
  testGfxPendingClear = display.splitCount();
  testGfxCycleTs = millis();
  Logger::info("test-gfx %s (%s%s)", on ? "ON" : "OFF", testGfxPatternName(testGfxPattern),
               (on && testGfxAutoCycle) ? ", auto-cycle 5s" : "");
}

// Фіксує конкретний патерн і вимикає авто-цикл: "test-gfx <pattern>" - це
// явний вибір, а не запит на демо.
void testgfx_set_pattern(TestGfxPattern pattern) {
  testGfxActive = true;
  testGfxAutoCycle = false;
  testGfxPattern = pattern;
  testGfxPendingClear = display.splitCount();
  testGfxCycleTs = millis();
  Logger::info("test-gfx ON (%s)", testGfxPatternName(pattern));
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
    if (dinoActive) return;  // змах пальцем під час стрибка - не запит яскравості
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
    if (dinoActive) return;
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

  #if !ESP8266
  mqtt.onConnect([](const MqttTransportClient& client) {
    _logger.info("MQTT connected       [%s:%d]", client.host.c_str(), client.port);
    publishDiscovery();
  });

  mqtt.onDisconnect([](const MqttTransportClient& client) {
    _logger.info("MQTT disconnected    [%s:%d]", client.host.c_str(), client.port);
  });

  mqtt.onConnectionFail([](const MqttTransportClient& client) {
    _logger.info("MQTT connect fail    [%s:%d], WiFi status=%d RSSI=%d dBm, %u B free (largest block %u B)",
                 client.host.c_str(), client.port, (int)WiFi.status(), (int)WiFi.RSSI(),
                 (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());
  });
  #endif

  mqtt.begin();
  _logger.info("topic prefix = '%s'", mqtt.keyGenerator().prefix().c_str());

#if HAS_CONSOLE_MQTT
  // ПІСЛЯ mqtt.begin(): до нього _keyGenerator ще nullptr, і топік для фільтра
  // ехо зарезолвився б без префікса, тобто фільтр не спрацював би.
  consoleMqtt.begin();
#endif

  // mqtt.publish(MQTT_LWT_TOPIC, "dummy-init-message", 1);
  scheduler.addCronTask(MQTT_HEARTBEAT_INTERVAL_MS, []() { mqtt.publish(MQTT_LWT_TOPIC, kMqttHeartbeatMessage); });

  /* #if !BOARD_ESP32_C6 || true
  mqtt.addStringListener("#", [](const char* topic, const char* payload) {
    // char t[9] = ""; ntp.ftime("%H:%M:%S", t, sizeof(t));
    _logger.debug(">>> %-50s %s", topic, payload);
  });
  #endif */

  #if !BOARD_ESP32_C6 || true
  mqtt.addStringListener("command/" MQTT_CLIENT_ID, [](const char* topic, const char* payload) {
    // char t[9] = ""; ntp.ftime("%H:%M:%S", t, sizeof(t));
    // Вивід команди повертається в "command/<client-id>/reply" - той самий
    // текст, що йде в serial-монітор (луну команди логує сам
    // runCommandWithResponse, тому окремий warn тут більше не потрібен).
    if (!commandQueue.submit(payload, mqttReplyTarget())) {
      // Явна відмова, а не тиша: інакше відправник чекав би відповіді, якої
      // не буде. Публікуємо прямо в reply-топік, повз чергу.
      mqtt.publish("command/" MQTT_CLIENT_ID "/reply", "busy: command queue is full");
      _logger.warn("command queue full, rejected: %s", payload);
    }
  });
  #endif

  registerJsonApiCommands();

  // LWT_TOPIC "mykola-lavryk:devices/mqtt-${PIOENV}/status"
  mqtt.addStringListener("devices/+/status", [](const char* topic, const char* payload) {
    char t[9] = ""; ntp.ftime("%H:%M:%S", t, sizeof(t));
    _logger.info("%s %-45.45s LWT:%s", t, topic, payload);
  });

  dispatcher.addListener(EVT_REBOOT, [](IEvent& e) { mqtt.disconnect("reboot"); });

#if BOARD_HAS_LIGHT_SENSOR
  // publish mqtt
  lightSensor.addListener([]() {
      _logger.debug("devices/" MQTT_CLIENT_ID "/light-sensor => %d", lightSensor.value());
      mqtt.publishNumber<int>("devices/" MQTT_CLIENT_ID "/light-sensor", (int)lightSensor.value());
  });
  _logger.info("devices/" MQTT_CLIENT_ID "/light-sensor MQTT done.");
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
    // Охайна табличка (CLAUDE.md, "Охайні логи") - той самий підхід, що
    // 'web status' (label, вирівняний на 9 символів, ':' одним стовпчиком).
    _logger.info("connected : %s", mqtt.isConnected() ? "yes" : "no");
    _logger.info("broker    : %s:%d", mqtt.host() ? mqtt.host() : "", (int)mqtt.port());
    _logger.info("security  : %s", mqtt.usesTls() ? "TLS" : "plain");
    _logger.info("client    : %s", mqtt.clientId() ? mqtt.clientId() : "");
    _logger.info("login     : %s", mqtt.username() ? mqtt.username() : "(anonymous)");
    _logger.info("prefix    : '%s'", mqtt.keyGenerator().prefix().c_str());
    _logger.info("published : %u", (unsigned)mqtt.publishedCount());
    _logger.info("received  : %u", (unsigned)mqtt.receivedCount());
    _logger.info("denied    : %u subscribe(s) rejected by broker (ACL)", (unsigned)mqtt.subscribeDeniedCount());
    // Запас стека мережевого таска. Потрібен не з цікавості: фільтр дзеркала
    // консолі (lib/ConsoleMqtt) виконує regexec() у КОЖНОМУ таску, що логує -
    // тобто й тут. У проєкті вже є урок про зрізаний стек TLS-таска, який
    // закінчився зависанням без panic-логу, тому це має бути видно командою.
    _logger.info("stack     : %u B headroom (network task)", (unsigned)mqtt.networkTaskStackHeadroom());
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

#if HAS_CONSOLE_MQTT
  commandHandler.registerCommand(
    "console-mqtt",
    "mirror the console to MQTT: console-mqtt [on|off | allow <tag> | deny <tag> | "
    "clear allow|deny | test <tag>]",
    [](const String args) {
      String rest = args;
      rest.trim();

      if (rest.length() == 0) {
        consoleMqtt.dumpStatus();
        return;
      }

      // Перший токен - підкоманда, решта рядка - її аргумент. Тег пробілів не
      // містить, але ріжемо так само: зайвий пробіл у кінці зніме trim().
      String verb = rest;
      String value = "";
      const int space = rest.indexOf(' ');
      if (space >= 0) {
        verb = rest.substring(0, space);
        value = rest.substring(space + 1);
        value.trim();
      }

      if (verb.equalsIgnoreCase("on") || verb.equalsIgnoreCase("off")) {
        consoleMqtt.setActive(verb.equalsIgnoreCase("on"), /*persist=*/true);
        return;
      }

      if (verb.equalsIgnoreCase("test")) {
        if (value.length() == 0) {
          _logger.info("use: console-mqtt test <tag>");
          return;
        }
        _logger.info("'%s' -> %s", value.c_str(), consoleMqtt.wouldPass(value.c_str()) ? "pass" : "blocked");
        return;
      }

      const bool isAllow = verb.equalsIgnoreCase("allow");
      const bool isDeny = verb.equalsIgnoreCase("deny");

      if (verb.equalsIgnoreCase("clear")) {
        if (value.equalsIgnoreCase("allow")) {
          consoleMqtt.clearRules(/*deny=*/false);
        } else if (value.equalsIgnoreCase("deny")) {
          consoleMqtt.clearRules(/*deny=*/true);
        } else {
          _logger.info("use: console-mqtt clear allow|deny");
          return;
        }
        _logger.info("%s rules cleared", value.c_str());
        return;
      }

      if (isAllow || isDeny) {
        if (value.length() == 0) {
          _logger.info("use: console-mqtt %s <tag> (hierarchical: 'mqtt' covers 'mqtt.send')",
                       verb.c_str());
          return;
        }
        char error[96] = "";
        if (!consoleMqtt.addRule(isDeny, value.c_str(), error, sizeof(error))) {
          _logger.error("rule '%s' rejected: %s", value.c_str(), error);
          return;
        }
        _logger.info("%s rule '%s' added", isDeny ? "deny" : "allow", value.c_str());
        return;
      }

      _logger.info("use: console-mqtt [on|off | allow <tag> | deny <tag> | clear allow|deny | "
                   "test <tag>]");
    }
  );
#endif

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

void dumpConfigStorage() {
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
    printSdStatus();
  } else if (section.equals("sd+")) {
    dumpSDInfo();
#endif
  } else {
    Logger::warn("use: status sys|cfg|sd|sd+|flash|flash+|littlefs");
  }
}

void setupSerialCommander() {
#if HAS_WEB_PORTAL
  // Пароль порталу інакше можна було б задати лише з самого порталу - тобто
  // з відкритої сторінки, яку до першого пароля бачить уся мережа. Тому
  // креденшели ставляться з консолі.
  commandHandler.registerCommand(
      "web", "web portal: status | auth <user> <pass> | auth off", [](const String args) {
        static TLogger _log{"web"};
        String rest = args;
        rest.trim();

        if (rest.length() == 0 || rest.startsWith("status")) {
          _log.info("server   : %s", webPortal.isRunning() ? "running" : "stopped");
          _log.info("auth     : %s", httpServer.hasAuth() ? "on (HTTP Basic)" : "off - open to everyone");
          _log.info("jobs     : %u pending", (unsigned)webPortal.jobs().pending());
          if (WiFi.isConnected()) _log.info("url      : http://%s/", WiFi.localIP().toString().c_str());
          if (netSupervisor.state() == NetworkSupervisorState::AP_MODE) {
            _log.info("hotspot  : http://%s/", WiFi.softAPIP().toString().c_str());
          }
          return;
        }

        if (!rest.startsWith("auth")) {
          _log.warn("use: web status | web auth <user> <pass> | web auth off");
          return;
        }

        rest = rest.substring(4);
        rest.trim();

        if (rest == "off") {
          webPortal.setCredentials("", "");
          _log.warn("auth disabled, restart to apply");
          return;
        }

        const int space = rest.indexOf(' ');
        if (space <= 0) {
          _log.warn("use: web auth <user> <pass> | web auth off");
          return;
        }

        String user = rest.substring(0, space);
        String pass = rest.substring(space + 1);
        user.trim();
        pass.trim();
        if (user.length() == 0 || pass.length() == 0) {
          _log.warn("both user and password are required");
          return;
        }

        webPortal.setCredentials(user, pass);
        _log.warn("credentials saved for '%s', restart to apply", user.c_str());
      });
#endif

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
    const unsigned long uptimeSec = millis() / 1000UL;
    const unsigned long uptimeDays = uptimeSec / 86400UL;
    const unsigned long uptimeHours = (uptimeSec % 86400UL) / 3600UL;
    const unsigned long uptimeMins = (uptimeSec % 3600UL) / 60UL;
    const unsigned long uptimeSecs = uptimeSec % 60UL;
    _log.info("uptime       : %lu s  (%lud %02luh %02lum %02lus)", uptimeSec, uptimeDays, uptimeHours,
              uptimeMins, uptimeSecs);
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
  commandHandler.registerCommand("heap-watch", "periodic 'heap' sampling: heap-watch [on|off]",
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
    }
  );

  // heap-watch увімкнений за замовчуванням (те саме, що ecoflow-watch) - вимкнення
  // застосовується одразу при старті, якщо збережене в NVS.
  if (!configStorage.getBool(CFG_HEAP_WATCH, true)) {
    scheduler.pause(heapWatchCronTaskId);
    static TLogger _log{"heap"};
    _log.warn("heap watch disabled");
  }

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
    Logger::info("rebooting");
    Journal::instance().flushBlocking(300);
    delay(100);  // не Serial.flush(): на USB CDC він викидає TX, а не дочікує
    ESP.restart();
#else
        SystemReset::reboot();
#endif
  });

  // command: watchdog
  //
  // Той самий патерн, що ecoflow-auto/ecoflow-sync: build-time дефолт
  // (WATCHDOG_ENABLED) + runtime-override у ConfigStorage, що діє з
  // наступного ребуту - не живий перемикач посеред сесії. Вимикати перед
  // sdbench/sdcrc/sdmap (свідомо блокують loop() на десятки секунд) і
  // вмикати назад після.
  commandHandler.registerCommand(
      "watchdog",
      "loop() hang watchdog, auto-reset on freeze: watchdog [on|off] (applies on next boot)",
      [](const String args) {
        static const TLogger _log{"wdog"};
        String value = args;
        value.trim();
        if (value.length() == 0) {
          String stored = configStorage.getString(CFG_WATCHDOG, "");
          _log.info("watchdog = %s%s",
                    stored.length() > 0 ? (stored.toInt() ? "on" : "off")
                                        : (WATCHDOG_ENABLED ? "on" : "off"),
                    stored.length() > 0 ? "" : " (build-time default)");
          return;
        }
        bool on = false;
        if (!parseBool(value, on)) {
          _log.warn("use: watchdog [on|off]");
          return;
        }
        configStorage.setString(CFG_WATCHDOG, on ? "1" : "0");
        _log.info("watchdog = %s (applies on next boot)", on ? "on" : "off");
      });

  registerNetCommand(commandHandler, netSupervisor);
  registerJournalCommand(commandHandler);

  // Єдиний виконавець команд і єдиний вхід. Читач serial більше не виконує
  // рядок сам - він кладе його в чергу, як і MQTT, веб та cron.
  commandQueue.begin([](const char* line) { commandHandler.execute(line); });
  commandHandler.setLineHandler([](const String& line) {
    static const TLogger log{"cmd"};
    if (!commandQueue.submit(line.c_str())) {
      log.warn("busy: command queue is full (%u slots), try again", (unsigned)CommandQueue::kSlots);
    }
  });
  // 'scan' лишається як коротший псевдонім 'net device wifi list'
  commandHandler.registerCommand("scan", "scan WiFi networks (alias of 'net device wifi list')",
                                 [](const String& args) {
                                   std::vector<std::string> known;
                                   for (const auto& c : netSupervisor.connections()) {
                                     known.push_back(c.ssid);
                                   }
                                   WiFi_scan(known);
                                 });

#if HAS_GMAIL_SENDER
  // command: mailto
  //
  // Той самий механізм відповіді, що для MQTT-команд, але з іншим приймачем -
  // вивід вкладеної команди їде листом. Тут це ще й єдиний користувач
  // EmailTarget; для команд за розкладом cron-лямбда так само захоплює
  // shared_ptr на приймач і віддає результат при кожному спрацюванні.
  commandHandler.registerCommand(
      "mailto", "run a command and send its output by email: mailto <address> <command>",
      [](const String& args) {
        static TLogger logger("mailto");

        const int spaceIdx = args.indexOf(' ');
        if (spaceIdx < 0) {
          logger.info("use: mailto <address> <command>");
          return;
        }

        const String address = args.substring(0, spaceIdx);
        String command = args.substring(spaceIdx + 1);
        command.trim();
        if (address.length() == 0 || command.length() == 0) {
          logger.info("use: mailto <address> <command>");
          return;
        }

        auto target = std::make_shared<EmailTarget>(mailer, address, String(PIO_PIOENV ": ") + command,
#if HAS_MQTT_CLIENT
                                                    &mqtt
#else
                                                    nullptr
#endif
        );
        // ПРЯМО ЗАРАЗ, повз чергу: вкладена команда мусить виконатись у цьому
        // ж виклику, інакше лист пішов би без її виводу.
        commandQueue.runNow(command.c_str(), std::move(target));
      });
  commandHandler.registerCommand("sendmail", "send an SMTP smoke-test email to " GMAIL_TEST_RECIPIENT,
                                 [](const String& args) { sendEmail(); });

  // command: smtp-probe
  //
  // Конект до SMTP-хоста без поштової бібліотеки. Потрібен, щоб відділити
  // "мережа/сервер недосяжні" від "бібліотека не читає відповідь": обидва
  // випадки в логах поштової бібліотеки виглядають однаково. Той самий підхід,
  // що `sdbb` для SD - перевірка найнижчого шару своїми руками. Саме ця
  // команда й довела, що на C6 винна була бібліотека, а не плата.
  commandHandler.registerCommand(
      "smtp-probe", "probe the SMTP host without the mail library: smtp-probe [port] (465 = TLS)",
      [](const String& args) {
        static TLogger logger("smtp");

        if (!WiFi.isConnected()) {
          logger.error("wifi is not connected");
          return;
        }

        // Порт з аргументу, інакше зібраний. 465 пробуємо через TLS ядра
        // (WiFiClientSecure, mbedTLS) - це рівно той шар, на який спирається
        // будь-яка поштова бібліотека без власного стека, тож проба каже, чи
        // взагалі можлива TLS-сесія з Gmail на цій платі.
        String portArg = args;
        portArg.trim();
        const uint16_t port = portArg.length() > 0 ? (uint16_t)portArg.toInt() : (uint16_t)GMAIL_SMTP_PORT;
        const bool useTls = (port == 465);

#if defined(ESP32)
        WiFiClientSecure tlsClient;
        if (useTls) {
          // Сертифікат навмисно не перевіряємо: тут перевіряється сама
          // можливість handshake, а не довіра до сервера.
          tlsClient.setInsecure();
        }
#endif
        WiFiClient plainClient;
#if defined(ESP32)
        Client& client = useTls ? static_cast<Client&>(tlsClient) : static_cast<Client&>(plainClient);
#else
        Client& client = plainClient;
        if (useTls) {
          logger.warn("TLS probe is ESP32-only, falling back to plain TCP");
        }
#endif

        logger.info("probing %s:%u (%s), %u B free, largest block %u B", GMAIL_SMTP_HOST, (unsigned)port,
                    useTls ? "TLS" : "plain", (unsigned)ESP.getFreeHeap(),
#if defined(ESP32)
                    (unsigned)ESP.getMaxAllocHeap()
#else
                    (unsigned)ESP.getMaxFreeBlockSize()
#endif
        );

        const uint32_t started = millis();
        if (!client.connect(GMAIL_SMTP_HOST, port)) {
          logger.error("connect to %s:%u failed after %lu ms", GMAIL_SMTP_HOST, (unsigned)port,
                       (unsigned long)(millis() - started));
          return;
        }
        logger.info("connected to %s:%u in %lu ms, waiting for greeting", GMAIL_SMTP_HOST, (unsigned)port,
                    (unsigned long)(millis() - started));

        // Банер (код 220) сервер шле сам, одразу після конекту: на 587 у
        // відкритому вигляді, на 465 - уже всередині TLS-сесії.
        String greeting;
        const uint32_t deadline = millis() + 5000;
        while (millis() < deadline && !greeting.endsWith("\n")) {
          while (client.available() > 0) {
            greeting += (char)client.read();
          }
          delay(10);
        }

        if (greeting.length() == 0) {
          logger.error("connected but silent for 5000 ms - no greeting");
          if (!useTls && port == 465) {
            logger.info("port 465 is implicit TLS - silence over plain TCP is expected here");
          }
        } else {
          greeting.trim();
          logger.info("greeting: %s", greeting.c_str());
        }
        client.stop();
      });
#endif

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
        Journal::instance().flushBlocking(300);
        delay(100);  // не Serial.flush(): на USB CDC він викидає TX, а не дочікує
        REG_WRITE(RTC_CNTL_OPTION1_REG, RTC_CNTL_FORCE_DOWNLOAD_BOOT);
        esp_restart();
      });
#endif

  registerSdCommands(commandHandler);

  commandHandler.registerCommand("flip", "flip display (180)", [](const String& args) { display_flip(); });

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

  // Реєструється поза #if - як flip/clock/brightness: список команд має бути
  // однаковим на всіх платах, а недоступність фічі видно з відповіді.
  commandHandler.registerCommand("dino", "Chrome Dino game on screen: dino on|off|test",
                                 [](const String& args) {
    if (args.equalsIgnoreCase("on")) {
      dino_set_active(true);
    } else if (args.equalsIgnoreCase("off")) {
      dino_set_active(false);
    } else if (args.equalsIgnoreCase("test")) {
#if HAS_DINO_GAME
      if (!dinoRenderer.ready()) {
        Logger::warn("dino: renderer not ready");
      } else {
        // Знову ж таки лише прапорець - малює loop() (див. dino_set_active).
        dinoTestMode = true;
        dinoActive = false;
        dinoPendingClear = display.splitCount();
        Logger::info("dino: sprite sheet mode ON (dino off to leave)");
      }
#else
      Logger::info("dino: display game not available on this board");
#endif
    } else if (args.length() == 0) {
#if HAS_DINO_GAME
      if (!dinoRenderer.ready()) {
        Logger::info("dino: renderer not ready");
      } else {
        const DinoGame& g = dinoRenderer.game();
        const DinoLayout& L = g.layout();
        Logger::info("dino: %s%s", dinoActive ? "ON" : "OFF",
                     dinoTestMode ? " (sprite sheet)" : "");
        Logger::info("  screen %dx%d, ground y=%d, dino %dx%d, jump %d px",
                     (int)L.viewW, (int)L.viewH, (int)L.groundY, (int)L.playerW,
                     (int)L.playerH, (int)L.jumpApex);
        Logger::info("  obstacle kinds: %u (large cactus %s)", (unsigned)L.obstacleCount,
                     L.obstacleCount > 1 ? "on" : "off");
        Logger::info("  score %u, high %u, speed %d px/s", (unsigned)g.score(),
                     (unsigned)g.highScore(), (int)g.speed());
        // Кадр збирається за splitCount() проходів loop(), тому ігрових
        // кадрів на секунду рівно стільки ж разів менше.
        const uint32_t lr = display.loopFrameRate();
        Logger::info("  loop %u/s -> game %u fps (%u strips per frame)", (unsigned)lr,
                     (unsigned)(lr / display.splitCount()), (unsigned)display.splitCount());
      }
#else
      Logger::info("dino: display game not available on this board");
#endif
    } else {
      Logger::info("use: dino on|off|test");
    }
  });

  commandHandler.registerCommand(
      "test-gfx",
      "display graphics test patterns: test-gfx on (cycles patterns every 5s) | off | "
      "bars|gray|gradient|frame|checker|primitives (pins one pattern)",
      [](const String& args) {
        if (args.equalsIgnoreCase("on")) {
          testGfxAutoCycle = true;
          testgfx_set_active(true);
        } else if (args.equalsIgnoreCase("off")) {
          testgfx_set_active(false);
        } else if (args.length() == 0) {
          Logger::info("test-gfx: %s (%s%s)", testGfxActive ? "ON" : "OFF",
                       testGfxPatternName(testGfxPattern),
                       (testGfxActive && testGfxAutoCycle) ? ", auto-cycle 5s" : "");
        } else {
          TestGfxPattern p;
          if (testGfxPatternFromName(args.c_str(), &p)) {
            testgfx_set_pattern(p);
          } else {
            Logger::info("use: test-gfx on|off|bars|gray|gradient|frame|checker|primitives");
          }
        }
      });

  commandHandler.registerCommand("brightness", "control screen brightness: brightness 0-100|auto", [](const String& args) {
    if (args.length() == 0) {
      Logger::info("use: brightness 0-100|auto");
    } else if (args.equalsIgnoreCase("auto")) {
#if BOARD_HAS_LIGHT_SENSOR
      display_brightness(lightSensor.value(), true);
      Logger::info(" isAutoBrighness = %s", isAutoBrightness ? "true" : "false");
#else
      Logger::info(" isAutoBrighness **disabled**");
#endif
    } else if (args.toInt() < 0 || args.toInt() > 100 || (args.toInt() == 0 && args != "0")) {
      // Обрізати до 100 мовчки не можна: uint8_t-параметр перетворив би 300
      // на 44, а "abc" (toInt() == 0) погасив би екран.
      Logger::warn("use: brightness 0-100|auto");
    } else {
      display_brightness(args.toInt(), false);
    }
  });

  registerBackgroundCommands(commandHandler);

  commandHandler.registerCommand("dump-asuswrt", "test AsusWRT", [](const String& args) { testAsusWRT(); });
  commandHandler.registerCommand("dump-asuswrt2", "test AsusWRT parser on /asus-get_clientlist.json (LittleFS)",
                                 [](const String& args) { testAsusWRT2(); });

  Logger::info("SerialCommander setup done");
}

void setupConfigStorage() {
  configStorage.begin(PIO_PIOENV);
  showClock = configStorage.getBool(CFG_SHOW_CLOCK, true);
  Logger::info("ConfigStorage init done");
}

void loadConfig() {
  showClock = configStorage.getBool(CFG_SHOW_CLOCK, true);
  isAutoBrightness = configStorage.getBool(CFG_SYS_AUTOBRIGHTNESS, false);
  // Ігровий режим НЕ відновлюється після ресету свідомо. По-перше, у гру,
  // яку ніхто не почав, грати нема кому - після перезавантаження доречніше
  // показати годинник. По-друге, це запобіжник: якби гра колись падала на
  // старті, збережений прапорець дав би boot-loop, з якого пристрій не
  // вийшов би сам. Рекорд при цьому зберігається (CFG_DINO_HIGHSCORE).
  // _apply: на старті ми лише ЧИТАЄМО збережене значення, тому писати його
  // назад у NVS (як робив display_brightness()) не потрібно.
  display_brightness_apply(configStorage.getInt(CFG_DISPLAY_BRIGHTNESS, 100), isAutoBrightness);
  Logger::info("ConfigStorage load done");

  Logger::info("\t- %s = %s", CFG_SHOW_CLOCK, showClock ? "ON" : "OFF");
  Logger::info("\t- %s = %s", CFG_SYS_AUTOBRIGHTNESS, isAutoBrightness ? "true" : "false");
  Logger::info("\t- %s = %d", CFG_DISPLAY_BRIGHTNESS, display.brightness());
  Logger::info("");
}

// Замість колишнього setupWiFi(): підняти NetworkSupervisor і віддати йому
// радіо. Виклик неблокуючий, як і раніше - FSM крутиться у власному
// FreeRTOS-таску, а все, що нижче в setup(), і так стоїть під
// WiFi.isConnected()-гардами.
void setupNetworkSupervisor() {
  NetworkSupervisorConfig cfg;
  // Ім'я env у SSID точки доступу: у мережі часто крутиться кілька плат.
  cfg.apSsid = std::string("ESP-") + PIO_PIOENV;
  cfg.apFallbackEnabled = true;
  // DHCP hostname = ім'я env за замовчуванням (WIFI_HOSTNAME, секрети),
  // щоб "ping esp32-c6-lcd096" резолвився без окремої настройки. Runtime-
  // перевизначення - 'net general hostname <name>', loadConfig() нижче
  // підхопить його з NVS і перекриє цей дефолт.
  cfg.hostname = WIFI_HOSTNAME;
  netSupervisor.setConfig(cfg);

  // Список мереж живе в NVS; loadConfig() перекриє щойно виставлений cfg
  // збереженим, якщо він там є.
  netSupervisor.loadConfig();

  // Порожній список - пробуємо файли з LittleFS (/network/*.nmconnection,
  // формат keyfile NetworkManager). Їх кладуть з компа через
  // 'pio run -t uploadfs', тобто плату можна спорядити мережами не
  // перекомпільовуючи прошивку й не набираючи нічого в консолі.
  //
  // Саме ЗАСІВ, а не постійне джерело: далі список живе в NVS, бо uploadfs
  // перезаписує розділ цілком і поховав би все додане командою 'net'.
  // Перечитати файли будь-коли - 'net connection reload'.
  if (netSupervisor.connections().empty()) {
    const size_t imported = importNetProfilesFromFs(netSupervisor);
    if (imported > 0) {
      Logger::info("NetworkSupervisor imported %u profile(s) from LittleFS",
                   (unsigned)imported);
    }
  }

  // Остання лінія оборони: прошитий перелік (src/WifiNetworks.hpp), щоб пристрій
  // не лишився без зв'язку після чистої прошивки з порожнім LittleFS.
  //
  // Свідомо на КОЖНОМУ старті, а не лише на порожній список: інакше додана в
  // таблицю мережа доїжджала б на плату тільки через erase-flash, а зміна
  // пароля в secrets.ini мовчки не мала б жодного ефекту. seedConnections()
  // додає лише відсутні SSID і не чіпає вже збережені - тому 'net connection
  // modify' не відкочується. Виняток один: видалену командою 'net connection
  // delete' прошиту мережу наступний ребут поверне; щоб вимкнути її назовсім -
  // 'net connection modify <id> connection.autoconnect no'.
  if (netSupervisor.seedConnections(kWifiNetworks) > 0) netSupervisor.saveConfig();

  // lastConnected інакше рахувався б від millis() і обнулявся на кожному
  // ребуті - тоді збережений порядок "останній вдалий першим" після рестарту
  // ставав би випадковим. ntp ще не стартував, але лямбда ліниво питає час
  // у момент підключення.
  netSupervisor.setClock([]() -> uint32_t {
    return ntp.isSynced() ? static_cast<uint32_t>(time(nullptr)) : 0u;
  });

  netSupervisor.addListener(&networkEventLogger);
  netSupervisor.begin();
  Logger::info("NetworkSupervisor started with %u profile(s)",
               (unsigned)netSupervisor.connections().size());
}

#if HAS_WEB_PORTAL
// Піднімає портал ОДИН раз і назавжди. Свідомо без жодної перевірки
// WiFi.isConnected(): AsyncWebServer слухає на всіх інтерфейсах lwIP, тож той
// самий сервер обслуговує і домашню мережу, і AP-fallback. Прив'язка до
// стану мережі лише створила б вікно, коли пристрій уже підняв точку доступу,
// а портал на ній ще не відповідає.
void setupWebPortal() {
  webPortal.addModule(&webWifiModule);
  webPortal.addModule(&webConsoleModule);
  webPortal.addModule(&webCommandsModule);
  webPortal.addModule(&webNvsModule);
   webPortal.addModule(&webFilesModule);
  webPortal.addModule(&webSystemModule);
#if HAS_SCREEN_MIRROR
  webPortal.addModule(&webScreenModule);
#endif
#if HAS_MQTT_CLIENT
  webPortal.addModule(&webMqttModule);
#endif
#if HAS_ECOFLOW_CLIENT
  webPortal.addModule(&webEcoflowModule);
#endif

  if (!webPortal.begin()) {
    Logger::error("WebPortal setup failed");
    return;
  }
  Logger::info("WebPortal setup done");
}
#endif

void setupLightSensor() {
#if BOARD_HAS_LIGHT_SENSOR
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
  // Під час гри hold - це стрибок, а не запит автояскравості (той самий
  // захист, що й у свайпах яскравості в setupTouchScreen()).
  touchController.events().onHold([](TouchPoint p, unsigned long ms) {
    if (dinoActive) return;
    configStorage.setBool(CFG_SYS_AUTOBRIGHTNESS, isAutoBrightness = true);
    display_brightness(lightSensor.value(), isAutoBrightness);
  });

  SwipeCallback onSwipe = [](TouchPoint s, TouchPoint e) {
    if (dinoActive) return;
    configStorage.setBool(CFG_SYS_AUTOBRIGHTNESS, isAutoBrightness = false);
  };

  touchController.events().onSwipeUp(onSwipe);
  touchController.events().onSwipeDown(onSwipe);
#endif
#endif
}

void drawSystemInfo() {
  char buf[120] = "";
  uint8_t row = 0;
  #if BOARD_ESP32_C6_LCD096
  constexpr uint8_t space = 2;
  constexpr uint8_t top = 2; // space
  constexpr uint8_t left = 2; // space
  #else
  constexpr uint8_t space = 5;
  constexpr uint8_t top = 10; // space * 2
  constexpr uint8_t left = 10; // space * 2
  #endif
  // img.fillRect(0, 30, 320, 65, BG_COLOR);

  uint32_t freeHeap = ESP.getFreeHeap();
  uint32_t cpuFreq = ESP.getCpuFreqMHz();
  uint32_t uptimeSec = millis() / 1000;

  display.setTextSize(1);
  display.setTextFont(1);
  display.setTextColor(TFT_DARKGREY);

#if defined(ESP32)
  display.setCursor(left, top + row++ * (space + display.fontHeight()));
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
  display.setCursor(left, top + row++ * (space + display.fontHeight()));
  snprintf(buf, sizeof(buf), "CPU: %u MHz", (unsigned)cpuFreq);
  display.print(buf);

  display.setCursor(left, top + row++ * (space + display.fontHeight()));
  snprintf(buf, sizeof(buf), "Loop rate: %u/s", (unsigned)display.loopFrameRate());
  display.print(buf);
#else
  display.setCursor(left, top + row++ * (space + display.fontHeight()));
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
  display.setCursor(left, top + row++ * (space + display.fontHeight()));
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
    display.setCursor(left, top + row++ * (space + display.fontHeight()));
    display.print(dumpPingStr);  // було: повторний виклик dumpPingStatsStr()
  }

  #if BOARD_ESP32_C6_LCD096
  // Вузький екран (160px) - без відсотків, тому й wifiSignalQuality() тут не
  // рахуємо (раніше передавався третім, зайвим аргументом на два %-специфікатори).
  snprintf(buf, sizeof(buf), "WiFi: %s (%d dBm)", WiFi.SSID().c_str(), WiFi.RSSI());
  #else
  snprintf(buf, sizeof(buf), "WiFi: %s (%d dBm / %d%%)", WiFi.SSID().c_str(), WiFi.RSSI(), wifiSignalQuality(WiFi.RSSI()));
  #endif
  display.setCursor(left, top + row++ * (space + display.fontHeight()));
  display.print(buf);


  snprintf(buf, sizeof(buf), "IP: %s", WiFi.localIP().toString().c_str());
  display.setCursor(left, top + row++ * (space + display.fontHeight()));
  display.print(buf);

  snprintf(buf, sizeof(buf), "Brightness: %d%% %s", display.brightness(), isAutoBrightness ? "(auto)" : "");
  display.setCursor(left, top + row++ * (space + display.fontHeight()));
  display.print(buf);

  #if BOARD_HAS_LIGHT_SENSOR
    // display.setTextSize(1);
    // display.setTextColor(TFT_DARKGREY);
    // display.setCursor(10, display.height() - 1 * (5 + display.fontHeight()));
    display.setCursor(left, top + row++ * (space + display.fontHeight()));
    display.printf("LightSensor: %4d (%3d%%)", lightSensor.read(), lightSensor.value());
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
  int16_t y = 8;
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
#if HAS_DINO_GAME
      // Ця сама кнопка в грі - кнопка стрибка. Друга cron-задача на той самий
      // пін не годиться: обидві читали б digitalRead і кожна рахувала б свій
      // фронт, тому вся логіка кнопки лишається тут, з розгалуженням за режимом.
      if (dinoActive) dinoRenderer.game().pressJump(now);
#endif
      Logger::info("Button pressed!");
    } else if (buttonState == LOW) {
      // loop (pressed) ....
      if (_pause) {
        // "hide/show" action done!
      } else if (now - flippButtonPressedTs > 3000UL) {
        // "hide/show" action done!
        _pause = true;
#if HAS_DINO_GAME
        // У грі довге утримання виходить із режиму, а не гасить екран:
        // інакше "затиснув для високого стрибка" закінчувалось би чорним
        // дисплеєм. Порогу 3 с ігрове утримання не досягає (holdExtraSec
        // це 0.18 с), тож із стрибком це не конфліктує.
        if (dinoActive) {
          dino_set_active(false);
          return;
        }
#endif
        if (display.brightness() == 0) {
          display_brightness(max(_brightness, (uint8_t)1), _autoBrightness);
        } else {
          _brightness = display.brightness();
          _autoBrightness = isAutoBrightness;
          display_brightness(0, false);
        }
      }
    } else if (flipButtonPressed) {
#if HAS_DINO_GAME
      if (dinoActive) {
        // Відпускання обрізає підйом - саме це дає керовану висоту стрибка.
        dinoRenderer.game().releaseJump(now);
      } else
#endif
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

void setupDinoGame() {
#if HAS_DINO_GAME
  if (!dinoRenderer.begin()) {
    Logger::warn("dino game disabled (renderer init failed)");
    return;
  }

  dinoRenderer.game().setHighScore((uint32_t)configStorage.getInt(CFG_DINO_HIGHSCORE, 0));

#if BOARD_HAS_TOUCHSCREEN
  // onTouch/onRelease, а НЕ onClick: onClick спрацьовує на відпусканні (і то
  // лише якщо не було hold чи свайпу), тобто стрибок або запізнювався б, або
  // не зараховувався взагалі при довгому натисканні.
  touchController.events().onTouch([](TouchPoint) {
    if (dinoActive) dinoRenderer.game().pressJump(millis());
  });
  touchController.events().onRelease([](TouchPoint) {
    if (dinoActive) dinoRenderer.game().releaseJump(millis());
  });
#endif

  // Рекорд пишемо не в момент game over, а окремим таском: запис у NVS
  // всередині кадру дав би помітний фриз саме тоді, коли гравець дивиться
  // на екран найуважніше.
  scheduler.addCronTask(1000, []() {
    if (!dinoRenderer.game().highScoreDirty()) return;
    const uint32_t hi = dinoRenderer.game().highScore();
    configStorage.setInt(CFG_DINO_HIGHSCORE, (int32_t)hi);
    dinoRenderer.game().clearHighScoreDirty();
    Logger::info("dino: new high score %u", (unsigned)hi);
  });

  Logger::info("Dino game setup done (hi %u)", (unsigned)dinoRenderer.game().highScore());
#endif
}

#if BLINK_LED_PIN
namespace {

// Один сегмент патерну: тримати рівень `on` протягом `ms`, тоді перейти
// до наступного сегменту (з циклічним поверненням на початок масиву).
struct BlinkSegment {
  uint16_t ms;
  bool on;
};

// Стан "пошук WiFi" — світиться постійно. Один сегмент з довільною
// тривалістю: рушій періодично переписує той самий рівень, видимого
// перемикання це не дає.
constexpr BlinkSegment kPatternSearchingWifi[] = {
    {1000, true},
};

// Стан "синхронізація часу" (лише перша спроба при завантаженні):
// два коротких спалахи, пауза 1с, повтор.
constexpr BlinkSegment kPatternSyncingTime[] = {
    {100, true}, {150, false}, {100, true}, {1000, false},
};

// Стан "підключення MQTT": один довгий спалах, пауза 1с, повтор.
constexpr BlinkSegment kPatternConnectingMqtt[] = {
    {500, true}, {1000, false},
};

// Стан "AP-режим": три коротких спалахи, пауза, три довгих спалахи,
// пауза, повтор із початку.
constexpr BlinkSegment kPatternApMode[] = {
    {80, true},  {100, false}, {80, true},  {100, false}, {80, true}, {500, false},
    {400, true}, {150, false}, {400, true}, {150, false}, {400, true}, {500, false},
};

// Робочий стан: один короткий спалах щосекунди.
constexpr BlinkSegment kPatternWorking[] = {
    {20, true}, {980, false},
};

struct BlinkPatternDef {
  const BlinkSegment* segments;
  uint8_t length;
};

// Порядок елементів має збігатись з enum BlinkState нижче — використовується
// як індекс масиву.
const BlinkPatternDef kBlinkPatterns[] = {
    {kPatternApMode, sizeof(kPatternApMode) / sizeof(kPatternApMode[0])},
    {kPatternSearchingWifi, sizeof(kPatternSearchingWifi) / sizeof(kPatternSearchingWifi[0])},
    {kPatternSyncingTime, sizeof(kPatternSyncingTime) / sizeof(kPatternSyncingTime[0])},
    {kPatternConnectingMqtt, sizeof(kPatternConnectingMqtt) / sizeof(kPatternConnectingMqtt[0])},
    {kPatternWorking, sizeof(kPatternWorking) / sizeof(kPatternWorking[0])},
};

enum class BlinkState : uint8_t {
  ApMode = 0,
  SearchingWifi = 1,
  SyncingTime = 2,
  ConnectingMqtt = 3,
  Working = 4,
};

const char* blinkStateName(BlinkState state) {
  switch (state) {
    case BlinkState::ApMode: return "ap_mode";
    case BlinkState::SearchingWifi: return "searching_wifi";
    case BlinkState::SyncingTime: return "syncing_time";
    case BlinkState::ConnectingMqtt: return "connecting_mqtt";
    default: return "working";
  }
}

// Ручний перемикач поверх авто-індикації: off/on тримають пін у фіксованому
// рівні й ігнорують стан плати, auto запускає драбинку currentBlinkState().
enum class BlinkMode : uint8_t { Off, On, Auto };

BlinkMode gBlinkMode = BlinkMode::Auto;

BlinkMode parseBlinkMode(const String& s) {
  if (s.equalsIgnoreCase("off")) return BlinkMode::Off;
  if (s.equalsIgnoreCase("on")) return BlinkMode::On;
  return BlinkMode::Auto;
}

const char* blinkModeName(BlinkMode mode) {
  switch (mode) {
    case BlinkMode::Off: return "off";
    case BlinkMode::On: return "on";
    default: return "auto";
  }
}

// Ручне форсування конкретного патерну (`blink 0..4`) — лишень у RAM, у NVS
// не пишеться. Перекриває off/on/auto, доки його не скасують іншим `blink`.
bool gBlinkForced = false;
BlinkState gForcedState = BlinkState::Working;

// Пріоритетна драбинка станів. NTP враховується лише до першої вдалої
// синхронізації при завантаженні — подальший періодичний re-sync патерн
// уже не змінює (навмисно, за задачею).
BlinkState currentBlinkState() {
  const NetworkSupervisorState netState = netSupervisor.state();
  if (netState == NetworkSupervisorState::STARTING_AP || netState == NetworkSupervisorState::AP_MODE) {
    return BlinkState::ApMode;
  }
  if (!netSupervisor.isConnected()) {
    return BlinkState::SearchingWifi;
  }

  static bool ntpSyncedOnce = false;
  if (!ntpSyncedOnce) {
    if (ntp.isSynced()) {
      ntpSyncedOnce = true;
    } else {
      return BlinkState::SyncingTime;
    }
  }

  if (!mqtt.isConnected()) {
    return BlinkState::ConnectingMqtt;
  }
  return BlinkState::Working;
}

}  // namespace
#endif

void setupBlinkLED() {
#if BLINK_LED_PIN
  pinMode(BLINK_LED_PIN, OUTPUT);
  digitalWrite(BLINK_LED_PIN, HIGH);  // вимкнено (інверсна логіка)

  gBlinkMode = parseBlinkMode(configStorage.getString(CFG_BLINK_LED, "auto"));

  scheduler.addCronTask(10, []() {
    static bool havePrevState = false;
    static BlinkState prevState = BlinkState::Working;
    static uint8_t segmentIndex = 0;
    static uint32_t segmentStartMs = 0;

    if (!gBlinkForced && gBlinkMode == BlinkMode::Off) {
      havePrevState = false;  // перехід у auto/forced нехай почне патерн з початку
      digitalWrite(BLINK_LED_PIN, HIGH);
      return;
    }
    if (!gBlinkForced && gBlinkMode == BlinkMode::On) {
      havePrevState = false;
      digitalWrite(BLINK_LED_PIN, LOW);
      return;
    }

    const BlinkState state = gBlinkForced ? gForcedState : currentBlinkState();
    const BlinkPatternDef& pattern = kBlinkPatterns[static_cast<uint8_t>(state)];
    const uint32_t now = millis();

    // Зміна стану завжди починає патерн з першого сегменту, а не
    // продовжує з довільної точки попереднього патерну.
    if (!havePrevState || state != prevState) {
      havePrevState = true;
      prevState = state;
      segmentIndex = 0;
      segmentStartMs = now;
      digitalWrite(BLINK_LED_PIN, pattern.segments[0].on ? LOW : HIGH);
      return;
    }

    if (now - segmentStartMs >= pattern.segments[segmentIndex].ms) {
      segmentIndex = (segmentIndex + 1) % pattern.length;
      segmentStartMs = now;
      digitalWrite(BLINK_LED_PIN, pattern.segments[segmentIndex].on ? LOW : HIGH);
    }
  });

  commandHandler.registerCommand(
      "blink", "LED control: blink [on|off|auto|0-4], no args - show status", [](const String& args) {
        if (args.isEmpty()) {
          Logger::info("blink: mode=%s%s", blinkModeName(gBlinkMode), gBlinkForced ? " (forced)" : "");
          if (gBlinkForced) {
            Logger::info("blink: forced pattern=%u (%s)", static_cast<unsigned>(gForcedState),
                         blinkStateName(gForcedState));
          }
          Logger::info("blink usage: blink [on|off|auto|0-4]");
          Logger::info("  off  - LED always off");
          Logger::info("  on   - LED always on");
          Logger::info("  auto - status indication (wifi search / time sync / mqtt connect / AP mode / working)");
          Logger::info("  0-4  - force one pattern for testing, not saved to NVS:");
          Logger::info("         0=ap_mode 1=searching_wifi 2=syncing_time 3=connecting_mqtt 4=working");
          return;
        }

        bool isNumeric = args.length() > 0;
        for (size_t i = 0; i < args.length(); ++i) {
          if (!isDigit(args[i])) {
            isNumeric = false;
            break;
          }
        }
        if (isNumeric) {
          const int n = args.toInt();
          if (n < 0 || n > 4) {
            Logger::info("blink: pattern index must be 0..4, got \"%s\"", args.c_str());
            return;
          }
          gForcedState = static_cast<BlinkState>(n);
          gBlinkForced = true;
          Logger::info("blink: forced pattern=%d (%s), not saved", n, blinkStateName(gForcedState));
          return;
        }

        if (!args.equalsIgnoreCase("off") && !args.equalsIgnoreCase("on") && !args.equalsIgnoreCase("auto")) {
          Logger::info("blink: unknown mode \"%s\" (usage: blink [on|off|auto|0-4])", args.c_str());
          return;
        }

        gBlinkForced = false;
        const BlinkMode requested = parseBlinkMode(args);
        gBlinkMode = requested;
        configStorage.setString(CFG_BLINK_LED, blinkModeName(requested));
        Logger::info("blink: mode=%s", blinkModeName(requested));
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

  // Logger::info("================ Display %dx%d", display.width(), display.height());

  scheduler.addCronTask(0, [p]() {
    // scheduler.loop() крутиться ВСЕРЕДИНІ транзакції кадру, тому цей таск
    // домалював би іконку поверх ігрової сцени.
#if HAS_DINO_GAME
    if (dinoActive || dinoTestMode) return;
#else
    if (dinoActive) return;
#endif
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

void setup() {
  uint32_t freeHeap = ESP.getFreeHeap();
  // Кожен крок ідеться через withTrace() (lib/Logger/Trace.hpp) - один
  // debug-рядок на крок: тривалість і heap до/після/дельта під тегом "trace".
  // Звідси видно, який саме setupXxx() важкий чи "з'їдає" heap, без ручного
  // millis()/getFreeHeap() у кожній функції (docs/tech_debt.md §4 - портал+
  // EcoFlow на тісному heap, для esp32-c3 саме там і знадобилось вперше).
  withTrace("setupSerial", []() { setupSerial(); });
  Logger::info("free heap memory from scratch: %u", freeHeap);

  withTrace("setupI2C", []() { setupI2C(); });  // обов'язково ДО setupTouchScreen()/setupImu() - шина спільна

  withTrace("setupSD", []() { setupSD(); });
  withTrace("setupLittleFS", []() { setupLittleFS(); });
  withTrace("setupConfigStorage", []() { setupConfigStorage(); });
  withTrace("setupSerialCommander", []() { setupSerialCommander(); });
  withTrace("setupBlinkLED", []() { setupBlinkLED(); });
  withTrace("setupDisplay", []() { setupDisplay(); });
  withTrace("setupTouchScreen", []() { setupTouchScreen(); });
  withTrace("setupImu", []() { setupImu(); });
  withTrace("setupNetworkSupervisor", []() { setupNetworkSupervisor(); });
#if HAS_WEB_PORTAL
  // Після setupNetworkSupervisor(): мережевий стек має бути ініціалізований
  // (WiFi.mode() всередині FSM), інакше AsyncTCP піднімається на ще
  // неіснуючому інтерфейсі. Самого ПІДКЛЮЧЕННЯ чекати не треба - його може
  // не бути взагалі.
  withTrace("setupWebPortal", []() { setupWebPortal(); });
#endif
  withTrace("setupNtpService", []() { setupNtpService(); });
  withTrace("setupBackgroundImage", []() { setupBackgroundImage(); });
  withTrace("setupLightSensor", []() { setupLightSensor(); });
  withTrace("setupMqttClient", []() { setupMqttClient(); });
#if HAS_ECOFLOW_CLIENT
  // Після setupNtpService(): REST-підпис EcoFlow використовує timestamp, а
  // MQTT-хендшейк - перевірку строку дії сертифіката.
  withTrace("setupEcoflow", []() { setupEcoflow(); });
#endif
  withTrace("setupFlipButton", []() { setupFlipButton(); });
  // після setupDisplay()/setupTouchScreen(): треба готові розміри екрана
  withTrace("setupDinoGame", []() { setupDinoGame(); });
  withTrace("setupWiFiIcon", []() { setupWiFiIcon(); });
  loadConfig();

  display.flush();
  Logger::debug("free heap memory: %u", ESP.getFreeHeap());
  Logger::info("");
  Logger::info("> Ready. Enter 'list' for comand list.");

  // Останнім рядком - після setup(), а не на початку: сам setup() законно
  // довший за timeoutMs (TLS-хендшейки EcoFlow/MQTT, декодування фону), і
  // жоден з його кроків не годує watchdog.
  //
  // Опційний, за зразком ECOFLOW_AUTOCONNECT/ECOFLOW_SYNC_ON_BOOT: build-time
  // дефолт (WATCHDOG_ENABLED, src/App/ConfigKeys.hpp) + runtime-override у
  // ConfigStorage ('watchdog on|off', застосовується з наступного ребуту).
  // Вимикається свідомо, а не годується з довгих SD-команд (sdbench/sdcrc/
  // sdmap) - ці команди живуть у lib/SDRawReader, яка НЕ повинна знати про
  // Watchdog застосунку (lib/SystemReset/Watchdog.hpp).
  String watchdogStored = configStorage.getString(CFG_WATCHDOG, "");
  const bool watchdogEnabled =
      watchdogStored.length() > 0 ? (watchdogStored.toInt() != 0) : (WATCHDOG_ENABLED != 0);
  if (watchdogEnabled) {
    Watchdog::begin();
  } else {
    Logger::warn("watchdog disabled ('watchdog on' + reboot to re-enable)");
  }
}

void loop() {
  // Найперший рядок - до будь-яких ранніх return (напр. isSdImageModeActive()
  // нижче): інакше та гілка, що їх має, watchdog не годує, і він спрацював
  // би на легітимному, просто довшому шляху.
  Watchdog::keepalive();

  remountCardIfMscAsked();

  // Режим знімання образу (sdimg, лише HAS_SD_WORKBENCH): дисплей навмисно не
  // малюється - SPI-шина спільна з карткою, і будь-яка транзакція панелі
  // посеред читання сектора зіпсувала б і кадр, і дані. Serial-команди
  // обслуговуємо далі, щоб режим можна було вимкнути ("sdimg off"). update()
  // лише кладе рядок у чергу - виконує його runNext(), без нього "sdimg off"
  // не спрацював би ніколи.
  if (isSdImageModeActive()) {
    sdImageHandleClient();
    commandHandler.update();
    commandQueue.runNext();
    delay(1);
    return;
  }

  display.startWrite();
  // На ESP32 журнал дренажить власний таск; тут — щоб та сама гілка працювала
  // на ESP8266, де RTOS немає. Повторний виклик безпечний: помпа одна за раз.
  Journal::instance().pump();

#if HAS_DINO_GAME
  const bool dinoOn = dinoActive && dinoRenderer.ready();
#else
  constexpr bool dinoOn = false;
#endif

  // doPing() блокує loop() до ~1 с раз на 5 с (див. коментар у src/ping.h).
  // Для годинника це непомітно, для гри - десятки згаяних кадрів, тобто
  // кактус "телепортується" крізь діно.
  if (!dinoOn) doPing();

#if HAS_DINO_GAME
  // Перемикання режиму лишає на екрані шматки попередньої картинки: кадр
  // збирається за DISPLAY_SPLIT_COUNT проходів, тому чистимо стільки ж смуг.
  if (dinoPendingClear) {
    display.clear();
    dinoPendingClear--;
  }
#endif
  // Перемикання патерну в авто-циклі - лише на початку ПОВНОГО кадру (та сама
  // причина, що в isFrameStart(): між ітераціями loop() у межах кадру сцена
  // мінятись не має, інакше кожна смуга показала б інший патерн).
  if (testGfxActive && testGfxAutoCycle && display.isFrameStart() &&
      millis() - testGfxCycleTs >= kTestGfxCycleMs) {
    testGfxPattern = testGfxNextPattern(testGfxPattern);
    testGfxPendingClear = display.splitCount();
    testGfxCycleTs = millis();
  }

  // Той самий сенс, що dinoPendingClear вище, для тестової таблиці.
  if (testGfxPendingClear) {
    display.clear();
    testGfxPendingClear--;
  }

  if (!dinoOn) {
    if (testGfxActive) {
      drawTestGfx(testGfxPattern);
    }
#if HAS_DINO_GAME
    else if (dinoTestMode) {
      dinoRenderer.renderSpriteSheet();
    }
#endif
    else {
      drawBackgroundImage();
      drawSystemInfo();
    }
  }
#if HAS_DINO_GAME
  else {
    // Фізика рухається лише на початку повного кадру, а сцена малюється
    // щосмуги - інакше кожна смуга показала б свою фазу руху.
    dinoRenderer.frame(display.isFrameStart());
    // Лічильник кадрів живе всередині loopFrameRate(), а той викликається
    // лише з drawSystemInfo() - тобто в ігровому режимі просто стояв би,
    // і зміряти FPS самої гри (те, заради чого він і потрібен) було б
    // неможливо. Тут викликаємо його рівно раз за ітерацію, як і там.
    display.loopFrameRate();
  }
#endif

  #if HAS_MQTT_CLIENT
  if (WiFi.isConnected()) {
    uint32_t t0 = millis();
    mqtt.loop();
    uint32_t dt = millis() - t0;
    if (dt > 200) {
      Logger::warn("mqtt.loop() took %ums", dt);
    }
    #if HAS_ECOFLOW_CLIENT
    // MQTT свідомо лишається активним і в грі - саме ним прилітає "dino off".
    // А EcoFlow тягне REST-запити й таки помітно рве кадр.
    if (!dinoOn) ecoflow.loop();
    #endif
  }
  #endif

  commandHandler.update();
  // Один виконавець на всі джерела, не більше однієї команди за ітерацію:
  // команда може блокувати на десятки секунд (sdbench, sdmap, scan).
  // webPortal.loop() гейтиться тим самим runNext(): якщо цієї ітерації вже
  // виконалась команда, задача порталу (скан ефіру, запис NVS) чекає
  // наступну ітерацію - інакше вони підсумовуються в одній ітерації й
  // наближають поріг Watchdog (docs/mqtt-web-handoff.md, "Точка F").
  if (!commandQueue.runNext()) {
#if HAS_WEB_PORTAL
    // Тут виконуються задачі, поставлені з HTTP: скан ефіру, connect, запис у
    // NVS. У таску сервера їм не місце - вони блокують на секунди (див.
    // WebJobQueue.hpp). Serial-команди з веб-консолі сюди більше не ходять -
    // з етапу 6 вони йдуть у CommandQueue разом з рештою джерел.
    webPortal.loop();
#endif
  }
#if HAS_DINO_GAME
  if (showClock && !dinoOn && !dinoTestMode && !testGfxActive) drawTime();
#else
  if (showClock && !dinoOn && !testGfxActive) drawTime();
#endif

  scheduler.loop();

  display.endWrite();

#if BOARD_HAS_TOUCHSCREEN
 touchController.update();
#endif
  updateImuFlip();

  display.flush();

  delay(1);
}
