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

#include <CommandQueue.hpp>
#include <ConfigStorage.hpp>
#include <Display.hpp>
#include <Journal.hpp>
#include <LittleFS.h>
#include <Logger.hpp>
#include <NetworkSupervisor.hpp>
#include <SerialCommander.hpp>
#include <TaskController.hpp>
#include <Trace.hpp>
#include <Watchdog.hpp>

#include <string>
#include <vector>

// EcoflowClient/EcoflowDeviceRegistry/WebEcoflowModule - через App/AppGlobals.hpp.
#include "Ecoflow/EcoflowSetup.hpp"

#include "features.h"
#include "App/AppGlobals.hpp"
#include "BackgroundImages.hpp"
#include "Input/Input.hpp"
#include "Mail/MailCommands.hpp"
#include "Mqtt/MqttSetup.hpp"
#include "Router/RouterTest.hpp"
#include "Screen/Background.hpp"
#include "Screen/DisplayBusYield.hpp"
#include "Screen/ScreenControl.hpp"
#include "Screen/ScreenManager.hpp"
#include "Screen/DinoScreen.hpp"
#include "Screen/MainScreen.hpp"
#include "Screen/TestGfxScreen.hpp"
#include "Screen/WifiIcon.hpp"
#include "Sd/Sd.hpp"
#include "Web/WebPortalSetup.hpp"
#include "System/BlinkLed.hpp"
#include "System/SystemControl.hpp"
#include "System/SystemStatus.hpp"
#include "TestGfx.hpp"
#include "SizeFormatter.hpp"
#include "ntp.h"
#include "ping.h"
#include "setup.h"
#include "wifi.h"
#include "netcli.h"  // після wifi.h: netcli викликає WiFi_scan()
#include "WifiNetworks.hpp"
#include "journalcli.h"



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


void setupSerialCommander() {
  registerWebCommands(commandHandler);
  registerSystemStatusCommands(commandHandler);
  registerSystemControlCommands(commandHandler);
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

  registerMailCommands(commandHandler);
  registerSdCommands(commandHandler);

  registerScreenControlCommands(commandHandler);
  registerScreenCommands(commandHandler);
  registerDinoCommands(commandHandler);
  registerTestGfxCommands(commandHandler);
  registerInputCommands(commandHandler);

  commandHandler.registerCommand("led", "control led: led on|off", [](const String& args) {
    if (args.equalsIgnoreCase("on")) {
      Logger::info("LED ON");
    } else if (args.equalsIgnoreCase("off")) {
      Logger::info("LED OFF");
    } else {
      Logger::info("use: led on|off");
    }
  });

  registerBackgroundCommands(commandHandler);
  registerRouterCommands(commandHandler);

  Logger::info("SerialCommander setup done");
}

void setupConfigStorage() {
  configStorage.begin(PIO_PIOENV);
  Logger::info("ConfigStorage init done");
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
  withTrace("setupBlinkLED", []() { setupBlinkLED(commandHandler); });
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
  withTrace("setupButtons", []() { setupButtons(); });
  // після setupDisplay()/setupTouchScreen(): треба готові розміри екрана
  withTrace("setupScreens", []() { setupScreens(); });
  withTrace("setupWiFiIcon", []() { setupWiFiIcon(); });
  loadScreenSettings();

  display.flush();
  Logger::debug("free heap memory: %u", ESP.getFreeHeap());
  Logger::info("");
  Logger::info("> Ready. Enter 'list' for comand list.");

  setupWatchdog();
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

  // Блокуючу фонову роботу пропускаємо на екранах реального часу (гра):
  // doPing() блокує loop() до ~1 с раз на 5 с (див. коментар у src/ping.h) -
  // для годинника непомітно, для гри - десятки згаяних кадрів, тобто кактус
  // "телепортується" крізь діно.
  const bool realtime = screens.active().realtime();
  if (!realtime) doPing();

  // Активний екран: перемикання (з команд), очищення смуг, одна смуга кадру.
  screens.loop(display.isFrameStart());

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
    if (!realtime) ecoflow.loop();
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
  scheduler.loop();

  display.endWrite();

#if BOARD_HAS_TOUCHSCREEN
 touchController.update();
#endif
  updateImuFlip();

  display.flush();

  delay(1);
}
