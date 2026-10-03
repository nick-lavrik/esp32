#include "MqttSetup.hpp"

#include <Logger.hpp>
#include <TLogger.hpp>

#include "App/AppGlobals.hpp"
#include "Discovery.hpp"
#include "JsonApi.hpp"

#if HAS_MQTT_CLIENT
#if defined(ESP8266)
#include <ESP8266WiFi.h>
#else
#include <WiFi.h>
#endif

#include <MqttReplyTarget.hpp>

#if HAS_MQTT_CLIENT
// Приймач відповідей на MQTT-команди. Один на пристрій, лінива ініціалізація
// (Meyer's singleton) - конструюється при першій команді, а не під час
// static-init, коли mqtt ще може бути не готовий.
//
// Топік БЕЗ префікса: його підставить MqttClient::resolveTopic() через
// MqttKeyGenerator, як і для будь-якого іншого топіка. Повний вигляд -
// "<prefix>/command/<client-id>/reply".
std::shared_ptr<ResponseTarget> mqttReplyTarget() {
  static std::shared_ptr<ResponseTarget> target =
      std::make_shared<MqttReplyTarget>(mqtt, "command/" MQTT_CLIENT_ID "/reply");
  return target;
}
#endif
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
    publishDiscovery();
  });

  mqtt.onDisconnect([](const MqttTransportClient& client) {
    _logger.info("MQTT disconnected    [%s:%d]", client.host.c_str(), client.port);
  });

  mqtt.onConnectionFail([](const MqttTransportClient& client) {
    _logger.info("MQTT connect fail    [%s:%d], WiFi status=%d RSSI=%d dBm, %u B free (largest block %u B)",
                 client.host.c_str(), client.port, (int)WiFi.status(), (int)WiFi.RSSI(), (unsigned)ESP.getFreeHeap(),
                 (unsigned)ESP.getMaxAllocHeap());
  });
#endif

  mqtt.begin();
  _logger.info("topic prefix = '%s'", mqtt.keyGenerator().prefix().c_str());

#if HAS_CONSOLE_MQTT
  // ПІСЛЯ mqtt.begin(): до нього _keyGenerator ще nullptr, і топік для фільтра
  // ехо зарезолвився б без префікса, тобто фільтр не спрацював би.
  consoleMqtt.begin();
#endif

  scheduler.addCronTask(MQTT_HEARTBEAT_INTERVAL_MS, []() { mqtt.publish(MQTT_LWT_TOPIC, kMqttHeartbeatMessage); });

  mqtt.addStringListener("command/" MQTT_CLIENT_ID, [](const char* topic, const char* payload) {
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

  registerJsonApiCommands();

  // LWT_TOPIC "mykola-lavryk:devices/mqtt-${PIOENV}/status"
  mqtt.addStringListener("devices/+/status", [](const char* topic, const char* payload) {
    char t[9] = "";
    ntp.ftime("%H:%M:%S", t, sizeof(t));
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
  mqtt.addNumberListener<int>("devices/+/light-sensor", [](const char* topic, int value) {
    char t[9] = "";
    ntp.ftime("%H:%M:%S", t, sizeof(t));
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
      "publish", "publish message in MQTT: publish <topic> <message>", [](const String args) {
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
      });

  commandHandler.registerCommand("mqtt-prefix",
                                 "get/set MQTT topic prefix (eg. dev/prod/qa/eu-west1): mqtt-prefix [prefix]",
                                 [](const String args) {
                                   if (args.length() == 0) {
                                     _logger.info("mqtt topic prefix = '%s'", mqtt.keyGenerator().prefix().c_str());
                                     return;
                                   }
                                   configStorage.setString(CFG_MQTT_TOPIC_PREFIX, args);
                                   _logger.info(
                                       "saved '%s' -> reboot required to take effect (topics already "
                                       "subscribed with old prefix)",
                                       args.c_str());
                                 });

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
            _logger.info("use: console-mqtt %s <tag> (hierarchical: 'mqtt' covers 'mqtt.send')", verb.c_str());
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

        _logger.info(
            "use: console-mqtt [on|off | allow <tag> | deny <tag> | clear allow|deny | "
            "test <tag>]");
      });
#endif

  _logger.info("%s:%d (%s) lwt:%s", MQTT_HOST, MQTT_PORT, MQTT_CLIENT_ID,
               mqtt.keyGenerator().key(MQTT_LWT_TOPIC).c_str());
#else
  Logger::warn("MQTT client disabled!");
#endif
}
