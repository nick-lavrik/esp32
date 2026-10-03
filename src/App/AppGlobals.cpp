// Означення спільних об'єктів застосунку (оголошення - AppGlobals.hpp).
//
// Порядок означень нижче - це порядок конструювання, і він має значення:
// конструктори частини об'єктів отримують інші (див. шапку AppGlobals.hpp).
// Новий глобал із такою залежністю додається сюди, після своїх залежностей.

#include "AppGlobals.hpp"

#include <LittleFS.h>

#include "Sd/Sd.hpp"

static TouchScreenConfig makeTouchScreenConfig() {
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
  c.rawMaxX = TFT_WIDTH;  // 172, нативна ширина панелі
  c.rawMinY = 0;
  c.rawMaxY = TFT_HEIGHT;  // 320, нативна висота панелі

  c.screenWidth = TFT_HEIGHT;  // 320 - екран у landscape
  c.screenHeight = TFT_WIDTH;  // 172

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
static MqttConfig makeMqttConfig() {
  MqttConfig config;
  config.host = MQTT_HOST, config.port = MQTT_PORT, config.clientId = MQTT_CLIENT_ID;
  config.username = MQTT_USERNAME;
  config.password = MQTT_PASSWORD;
  config.lwtTopic = MQTT_LWT_TOPIC;
  config.lwtOfflineMessage = MQTT_LWT_MSG_OFFLINE;
  config.lwtOnlineMessage = MQTT_LWT_MSG_ONLINE;
  config.prefix = MQTT_TOPIC_PREFIX;  // build-time дефолт; runtime override - setupMqttClient()

#if defined(ECOFLOW_MQTT_SHARE_CLIENT)
  // EcoflowClient сидить на цьому самому клієнті (docs/tech_debt.md, "План:
  // спільний MqttClient") - його quota-повідомлення бувають до ~2 КБ
  // (EcoflowClient::makeMqttConfig(), той самий орієнтир), дефолтні 2 КБ
  // ризикують обрізати найбільші пакети.
  config.rootSubscribeBufferSize = 4 * 1024;
#endif

  return config;
}
#endif

NtpService ntp;
EventDispatcher dispatcher;
TaskController scheduler;
ConfigStorage configStorage;
JpegImage spaceImage;
SerialCommander commandHandler;

// Один вхід для всіх джерел команд (serial, MQTT, веб, cron) і один виконавець
// у loop(). Див. lib/CommandQueue.
CommandQueue commandQueue;

#if HAS_MQTT_CLIENT
MqttClient mqtt(makeMqttConfig());
// runtime override поверх MqttConfig::prefix; заповнюється лише за наявності
// CFG_MQTT_TOPIC_PREFIX в ConfigStorage, див. setupMqttClient()
MqttKeyGenerator mqttTopicPrefixOverride;
#endif

#if HAS_CONSOLE_MQTT
// Дзеркало консолі в "<prefix>/console/<MQTT_CLIENT_ID>" (lib/ConsoleMqtt).
// Топік БЕЗ префікса - його підставить MqttClient::resolveTopic(), як і для
// reply-топіка. Клієнт і сховище передані явно: щоб посадити дзеркало на
// інший MqttClient, правиться цей рядок, а не бібліотека.
ConsoleMqtt consoleMqtt(mqtt, configStorage, "console/" MQTT_CLIENT_ID);
#endif

#if HAS_ECOFLOW_CLIENT
static EcoflowClient::Config makeEcoflowConfig() {
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
#if defined(ECOFLOW_MQTT_PROXY_HOST)
  // Стадія 1 підтверджена (docs/tech_debt.md, "MQTT-проксі"): на платах без
  // PSRAM пряме TLS-з'єднання до mqtt-e.ecoflow.com і портал разом не
  // влазять у heap (~57 КБ на mbedTLS-буфери). TLS переносимо на
  // mosquitto-проксі (rpi5, тримає ОДНУ TLS-сесію на весь LAN), сюди
  // приходить лише розшифрований plain MQTT. mqttUsername/mqttHost вище й
  // далі визначають КАНАЛ і схему топіків (buildRootTopic/buildClientId) -
  // це реальний акаунт EcoFlow, автентифікація на його брокері вже зроблена
  // самим проксі. proxyUsername/Password - ОКРЕМІ, ЛОКАЛЬНІ LAN-креденшли
  // listener'а на rpi5, не облікові дані EcoFlow.
  config.proxyHost = ECOFLOW_MQTT_PROXY_HOST;
  config.proxyUsername = ECOFLOW_MQTT_PROXY_USERNAME;
  config.proxyPassword = ECOFLOW_MQTT_PROXY_PASSWORD;
#endif
  return config;
}

#if defined(ECOFLOW_MQTT_SHARE_CLIENT) && HAS_MQTT_CLIENT
// Спільний MqttClient (docs/tech_debt.md, "План: спільний MqttClient") -
// креди proxy й загального `mqtt` на цій платі вже сьогодні той самий
// аліас у secrets.ini (mqtt_username/password == ecoflow_proxy_username/
// password). `mqtt` оголошено вище - вже сконструйований на цей момент.
EcoflowClient ecoflow(makeEcoflowConfig(), &mqtt);
#else
EcoflowClient ecoflow(makeEcoflowConfig());
#endif
EcoflowDeviceRegistry ecoflowDevices;
#endif

HttpServer httpServer(HttpServerConfig{});

// Менеджер WiFi: тримає список мереж у NVS (namespace той самий, що й у
// configStorage - PIO_PIOENV) і сам веде підключення. Керується командою 'net',
// див. src/Net/NetCli.cpp.
NetworkSupervisor netSupervisor(&configStorage);

#if HAS_WEB_PORTAL
// Веб-портал. Живе незалежно від того, чи є підключення до роутера: коли
// жодної збереженої мережі не видно, NetworkSupervisor піднімає власну точку
// доступу ("ESP-<env>"), і сторінка доступна на ній - саме тоді вона й
// потрібна найбільше. Тому httpServer.begin() робиться один раз на старті і
// не гаситься при зміні стану мережі.
WebWifiModule webWifiModule(netSupervisor);
WebConsoleModule webConsoleModule;
WebCommandsModule webCommandsModule(
    commandHandler, [](const char* line) { return commandQueue.submit(line); }, configStorage);
WebNvsModule webNvsModule(configStorage);
// Місткість розділу окремим замиканням: usedBytes()/totalBytes() є в
// LittleFSFS, але не в fs::FS, через яке модуль дивиться на файлову систему.
// Спільна і для WebFilesModule, і для WebSystemModule (вкладка System) -
// друге дзеркалило б перше, якби лишилось окремим замиканням там само.
static auto littleFsUsage = [](size_t& used, size_t& total) {
  used = LittleFS.usedBytes();
  total = LittleFS.totalBytes();
  return total > 0;
};
WebFilesModule webFilesModule(LittleFS, "LittleFS", littleFsUsage);
#if BOARD_HAS_SD
// getSdCardInfo() - src/Sd/SdCard.cpp.
WebSystemModule webSystemModule(littleFsUsage, getSdCardInfo);
#else
WebSystemModule webSystemModule(littleFsUsage);
#endif
#if HAS_SCREEN_MIRROR
// Дзеркало екрана. Плата без спрайта кадру (esp32-c3) або з 1bpp-панеллю
// (esp8266) віддавати браузеру нічого не може - там розділу просто немає
// (див. HAS_SCREEN_MIRROR у include/features.h).
WebScreenModule webScreenModule;
#endif
#if HAS_MQTT_CLIENT
// Розділ "mqtt": стан ЗАГАЛЬНОГО клієнта (mqtt/consoleMqtt вище в цьому
// файлі) - НЕ EcoflowClient, у нього свій розділ нижче.
#if HAS_CONSOLE_MQTT
WebMqttModule webMqttModule(mqtt, commandQueue, consoleMqtt, kMqttHeartbeatMessage, MQTT_HEARTBEAT_INTERVAL_MS);
#else
WebMqttModule webMqttModule(mqtt, commandQueue, kMqttHeartbeatMessage, MQTT_HEARTBEAT_INTERVAL_MS);
#endif
#endif
#if HAS_ECOFLOW_CLIENT
// Розділ живе в src/Ecoflow, не в lib/WebPortal - див. коментар у
// WebEcoflowModule.hpp. ecoflow/ecoflowDevices - вище в цьому файлі.
WebEcoflowModule webEcoflowModule(ecoflow, ecoflowDevices);
#endif
WebPortal webPortal(httpServer, configStorage);
#endif

// Обидва об'єкти визначені БЕЗУМОВНО, навіть коли BOARD_HAS_DISPLAY=0
// (env:esp32-c3). Причина: display.* і displayConfig зустрічаються в src/
// в сотнях місць, і обвішувати кожне "#if BOARD_HAS_DISPLAY" означало б
// розділити на дві гілки тисячі рядків. Замість цього на платі без
// дисплея сам Display стає порожнім: TFT_eSPI там - заглушка з
// include/Setup_Headless.h, усі методи inline й no-op, тому компілятор
// прибирає ці виклики цілком (у прошивці не лишається ні коду, ні буферів).
//
// Драйвер володіє панеллю (і спрайтом смуги) - окремого глобала tft немає;
// тип DisplayDriver обирає lib/Display/DisplayDriver.hpp за BOARD_*.
DisplayDriver displayDriver;
Display display(displayDriver);

TouchScreenConfig displayConfig = makeTouchScreenConfig();

#if BOARD_HAS_LIGHT_SENSOR
AnalogSensor lightSensor(LIGHT_SENSOR_PIN, 0, 1855, 100, 0, 5);
#endif

#if BOARD_HAS_TOUCHSCREEN
TouchPointMapper mapper(displayConfig);
TouchEvents touch(displayConfig);
TouchController touchController;
#endif

#if HAS_GMAIL_SENDER
GmailSender mailer(GMAIL_EMAIL, GMAIL_PASSWORD, "ESP32 Device");
#endif
