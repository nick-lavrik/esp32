#pragma once

// Спільні об'єкти застосунку - extern-оголошення для всіх TU у src/.
//
//   #include "App/AppGlobals.hpp"
//   commandQueue.submit("heap");
//   display.flush();
//
// Означення - лише в AppGlobals.cpp, і саме там вони й мають лишатись.
// Порядок ініціалізації глобалів МІЖ TU у C++ не визначений, а частина цих
// об'єктів отримує в конструкторі інші (Display <- DisplayDriver,
// NetworkSupervisor <- ConfigStorage, EcoflowClient <- MqttClient,
// TouchEvents <- displayConfig, WebPortal <- HttpServer). Усередині одного
// TU порядок = порядок означень, тому нові глобали з такою залежністю
// означаються там само, а не в модулі, що ними користується. Стан, потрібний
// лише одному модулю, живе в самому модулі, не тут.

#include <Arduino.h>
#include <TouchScreenConfig.h>

#include <CommandQueue.hpp>
#include <ConfigStorage.hpp>
#include <Display.hpp>
#include <EventDispatcher.hpp>
#include <HttpServer.hpp>
#include <JpegImage.hpp>
#include <NetworkSupervisor.hpp>
#include <NtpService.hpp>
#include <SerialCommander.hpp>
#include <TaskController.hpp>

#include "ConfigKeys.hpp"
#include "features.h"

#if HAS_MQTT_CLIENT
#include <MqttClient.hpp>
#include <MqttKeyGenerator.hpp>
#endif
#if HAS_CONSOLE_MQTT
#include <ConsoleMqtt.hpp>
#endif
#if HAS_ECOFLOW_CLIENT
#include "Ecoflow/EcoflowClient.hpp"
#include "Ecoflow/EcoflowDeviceRegistry.hpp"
#endif
#if BOARD_HAS_TOUCHSCREEN
#include <TouchController.h>
#include <TouchEvents.h>
#include <TouchPointMapper.h>
#endif
#if HAS_GMAIL_SENDER
#include <GmailSender.hpp>
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
#if HAS_ECOFLOW_CLIENT
#include "Ecoflow/WebEcoflowModule.hpp"
#endif
#endif

// Подія EventDispatcher: перед перезавантаженням (команда 'reboot').
inline constexpr const char* EVT_REBOOT = "reboot";

extern NtpService ntp;
extern EventDispatcher dispatcher;
extern TaskController scheduler;
extern ConfigStorage configStorage;
// Фонове зображення (LittleFS) - його ж обробляють команди ефектів (blur, tint, ...).
extern JpegImage spaceImage;
extern SerialCommander commandHandler;
// Один вхід для всіх джерел команд (serial, MQTT, веб, cron) і один виконавець
// у loop(). Див. lib/CommandQueue.
extern CommandQueue commandQueue;

#if HAS_MQTT_CLIENT
// Періодичний "доказ життя" в LWT-топік, окремо від самого LWT (offline/online
// шле брокер/клієнт лише на конект/розрив) - щоб споживач бачив пристрій
// живим і між цими подіями, а не лише в момент (пере)з'єднання. Іменована
// константа, а не літерал у cron-виклику (setupMqttClient()): те саме
// значення показує вкладка MQTT (WebMqttModule) - інакше сторінка могла б
// мовчки розійтись зі справжнім інтервалом/повідомленням.
#ifndef MQTT_HEARTBEAT_INTERVAL_MS
#define MQTT_HEARTBEAT_INTERVAL_MS (5 * 60 * 1000UL)
#endif
inline constexpr const char* kMqttHeartbeatMessage = "heartbeat";

extern MqttClient mqtt;
// runtime override поверх MqttConfig::prefix; заповнюється лише за наявності
// CFG_MQTT_TOPIC_PREFIX в ConfigStorage, див. setupMqttClient()
extern MqttKeyGenerator mqttTopicPrefixOverride;
#endif

#if HAS_CONSOLE_MQTT
extern ConsoleMqtt consoleMqtt;
#endif

#if HAS_ECOFLOW_CLIENT
extern EcoflowClient ecoflow;
extern EcoflowDeviceRegistry ecoflowDevices;
#endif

extern HttpServer httpServer;
extern NetworkSupervisor netSupervisor;

#if HAS_WEB_PORTAL
extern WebWifiModule webWifiModule;
extern WebConsoleModule webConsoleModule;
extern WebCommandsModule webCommandsModule;
extern WebNvsModule webNvsModule;
extern WebFilesModule webFilesModule;
extern WebSystemModule webSystemModule;
#if BOARD_HAS_SD
// Провайдер картки для вкладки System. Означена в src/main.cpp поруч із
// dumpSDInfo() - саме там відомо, яка шина (SD чи SD_MMC) на цій платі.
bool getSdCardInfo(WebSystemSdInfo& out);
#endif
#if HAS_SCREEN_MIRROR
extern WebScreenModule webScreenModule;
#endif
#if HAS_MQTT_CLIENT
extern WebMqttModule webMqttModule;
#endif
#if HAS_ECOFLOW_CLIENT
extern WebEcoflowModule webEcoflowModule;
#endif
extern WebPortal webPortal;
#endif

// Обидва об'єкти визначені БЕЗУМОВНО, навіть коли BOARD_HAS_DISPLAY=0
// (env:esp32-c3) - див. коментар в AppGlobals.cpp.
extern DisplayDriver displayDriver;
extern Display display;

extern TouchScreenConfig displayConfig;

#if BOARD_HAS_TOUCHSCREEN
extern TouchPointMapper mapper;
extern TouchEvents touch;
extern TouchController touchController;
#endif

#if HAS_GMAIL_SENDER
extern GmailSender mailer;

// Куди шле дим-тест "sendmail". Дефолт - на власну адресу відправника: лист
// сам собі однаково проходить весь шлях через SMTP, а в коді не лишається
// прошитої чужої адреси.
#ifndef GMAIL_TEST_RECIPIENT
#define GMAIL_TEST_RECIPIENT GMAIL_EMAIL
#endif
#endif
