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
#if defined(BOARD_ESP8266)
#include <ESP8266WiFi.h>
#else
#include <WiFi.h>
#endif

#include <CommandQueue.hpp>
#include <Journal.hpp>
#include <Logger.hpp>
#include <SerialCommander.hpp>
#include <TaskController.hpp>
#include <Trace.hpp>
#include <Watchdog.hpp>

#include "features.h"
#include "App/AppGlobals.hpp"
#include "Ecoflow/EcoflowSetup.hpp"
#include "Input/Input.hpp"
#include "Mail/MailCommands.hpp"
#include "Mqtt/MqttSetup.hpp"
#include "Net/NetCli.hpp"
#include "Net/NetworkSetup.hpp"
#include "Net/Ntp.hpp"
#include "Net/Ping.hpp"
#include "Router/RouterTest.hpp"
#include "Screen/Background.hpp"
#include "Screen/DinoScreen.hpp"
#include "Screen/ScreenControl.hpp"
#include "Screen/ScreenManager.hpp"
#include "Screen/TestGfxScreen.hpp"
#include "Screen/WifiIcon.hpp"
#include "Sd/Sd.hpp"
#include "System/Battery.hpp"
#include "System/BlinkLed.hpp"
#include "System/JournalCli.hpp"
#include "System/SerialSetup.hpp"
#include "System/Storage.hpp"
#include "System/SystemControl.hpp"
#include "System/SystemStatus.hpp"
#include "Web/WebPortalSetup.hpp"

void setupSerialCommander() {
  registerWebCommands(commandHandler);
  registerSystemStatusCommands(commandHandler);
  registerSystemControlCommands(commandHandler);
  registerBatteryCommands(commandHandler);
  registerNetCommands(commandHandler);
  registerJournalCommands(commandHandler);

  // Єдиний виконавець команд і єдиний вхід. Читач serial більше не виконує
  // рядок сам - він кладе його в чергу, як і MQTT, веб та cron.
  commandQueue.begin([](const char* line) { commandHandler.execute(line); });
  commandHandler.setLineHandler([](const String& line) {
    static const TLogger log{"cmd"};
    if (!commandQueue.submit(line.c_str())) {
      log.warn("busy: command queue is full (%u slots), try again", (unsigned)CommandQueue::kSlots);
    }
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
  // doPing() блокує loop() до ~1 с раз на 5 с (див. коментар у src/Net/Ping.cpp) -
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
