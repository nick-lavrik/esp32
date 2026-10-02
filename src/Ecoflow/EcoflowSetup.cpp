#include "EcoflowSetup.hpp"

#if HAS_ECOFLOW_CLIENT

#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>

#include <CommandArgs.hpp>
#include <TLogger.hpp>
#include <vector>

#include "App/AppGlobals.hpp"
#include "EcoflowJournalView.hpp"

namespace {
// Дамп телеметрії EcoFlow у лог. Вимкнений за замовчуванням: пристрій шле
// quota кілька разів на секунду і десятками параметрів у кожному повідомленні.
bool ecoflowVerbose = false;
}  // namespace

void setupEcoflow() {
  static TLogger _logger{"ecoflow"};

  // Журнали переходів grid - завантажити "hot" стан із NVS (переживає
  // ребут), застосувати збережений перемикач запису. Викликати ПІСЛЯ
  // configStorage.begin() (setupConfigStorage(), раніше в setup()) - інакше нема звідки
  // читати.
  //
  // Boot-запис для кожного пристрою - НЕ тут: recordBootForAll() навмисно
  // відкладено до cron-умови нижче, що вже чекає ntp.isSynced() &&
  // WiFi.isConnected() перед першим MQTT/REST-конектом. До цього моменту
  // time(nullptr) ще не синхронізований, і Boot із заниженим epoch ламав
  // межу AGE сусіднього Transition у ecoflow-journal show (docs/ecoflow.md,
  // розділ «Журнал переходів grid»).
  ecoflowDevices.beginJournals(configStorage);
  String journalStored = configStorage.getString(CFG_ECOFLOW_JOURNAL, "");
  if (journalStored.length() > 0 && journalStored.toInt() == 0) {
    ecoflowDevices.setJournalPersistenceEnabledForAll(false);
  }
  // Live-чекпоінт - раз на 5 хв для кожного пристрою: звужує "невідоме
  // вікно" після наступного ребута до цього інтервалу замість "з часу
  // останнього справжнього переходу" (могло бути тижнями). Пише лише в
  // NVS (EcoflowGridJournal), MQTT не чіпає.
  scheduler.addCronTask(5 * 60 * 1000UL, []() { ecoflowDevices.recordLiveCheckpointForAll(); });

// Спільне з'єднання (ECOFLOW_MQTT_SHARE_CLIENT): connect/disconnect/fail
// цього самого сокета вже логує mqtt.onConnect/... (setupMqttClient()) -
// MqttClient тримає по ОДНОМУ слоту на кожен колбек, другий виклик тут
// мовчки перезаписав би той, що вже зареєстрований для загального mqtt.
#if defined(ESP32) && !defined(ECOFLOW_MQTT_SHARE_CLIENT)
  ecoflow.onMqttConnect([](MqttTransportClient& client) {
    _logger.info("MQTT connected       [%s:%d]", client.host.c_str(), client.port);
  });

  ecoflow.onMqttDisconnect([](const MqttTransportClient& client) {
    _logger.info("MQTT disconnected    [%s:%d]", client.host.c_str(), client.port);
  });

  ecoflow.onMqttConnectionFail([](const MqttTransportClient& client) {
    _logger.info("MQTT connect fail    [%s:%d], WiFi status=%d RSSI=%d dBm, %u B free (largest block %u B)",
                 client.host.c_str(), client.port, (int)WiFi.status(), (int)WiFi.RSSI(), (unsigned)ESP.getFreeHeap(),
                 (unsigned)ESP.getMaxAllocHeap());
  });
#endif

  // Зміна наявності мережі - головна подія, яку тут відслідковують: разом із
  // нею друкуємо, СКІЛЬКИ пристрій пробув у попередньому стані, і дзеркалимо
  // поточний стан у MQTT (retained - новий підписник одразу бачить поточний
  // стан, не чекаючи наступного переходу). Персистентна історія переходів -
  // в EcoflowGridJournal (NVS, EcoflowDeviceRegistry::setGrid()), не тут -
  // цей колбек лише логує й дзеркалить, нічого не записує сам.
  ecoflowDevices.onGridChange(
      [](const EcoflowDeviceState& state, EcoflowGridState previousGrid, uint32_t previousDurationSec) {
        if (previousGrid == EcoflowGridState::Unknown) {
          // Перше визначення після старту - не перехід, тривалості ще немає.
          _logger.info("%s: %s (initial)", state.info->name, ecoflowGridStateName(state.grid));
        } else {
          const EcoflowGridJournal* journal = ecoflowDevices.journalAt(state.journalIndex);
          _logger.warn("%s: %s -> %s after %s (change #%u all-time)", state.info->name,
                       ecoflowGridStateName(previousGrid), ecoflowGridStateName(state.grid),
                       EcoflowDeviceRegistry::formatDurationSeconds(previousDurationSec).c_str(),
                       (unsigned)(journal != nullptr ? journal->totalChangeCount() : 0));
        }

#if HAS_MQTT_CLIENT
        // Payload - свідомо мінімальний (не "усі параметри"): grid - тригер,
        // charge/remain - контекст "чи вистачить батареї", timestamp - RFC3339 з
        // офсетом (NtpService::ftime, %o). Без changeCount/previousDurationSec/
        // gridInferred - docs/tech_debt.md розділ 8 пояснює, чому саме так.
        char timestamp[32] = "";
        ntp.ftime("%Y-%m-%dT%H:%M:%S%o", timestamp, sizeof(timestamp));

        String payload = "{\"grid\":\"";
        payload += ecoflowGridStateName(state.grid);
        payload += "\",\"timestamp\":\"";
        payload += timestamp;
        payload += "\",\"charge\":";
        payload += state.hasSoc() ? String((int)state.socPercent) : String("null");
        payload += ",\"remain\":";
        payload += state.remainTimeMinutes >= 0 ? String(state.remainTimeMinutes) : String("null");
        payload += "}";

        const String topic = "devices/" MQTT_CLIENT_ID "/ecoflow/" + String(state.info->serialNumber) + "/grid";
        mqtt.publish(topic.c_str(), payload.c_str(), /*retained=*/true);
#endif
      });

  ecoflowDevices.onSocChange([](const EcoflowDeviceState& state, int8_t previousSoc) {
    if (!ecoflowVerbose) {
      return;
    }
    _logger.info("%s: charge %d%% -> %d%%", state.info->name, static_cast<int>(previousSoc),
                 static_cast<int>(state.socPercent));
  });

  // Пристрій, від якого давно нічого не чути, вважаємо офлайн: /status
  // приходить не завжди, а тиша в quota - надійніший сигнал.
  scheduler.addCronTask(60 * 1000UL, []() { ecoflowDevices.expireStale(); });

  ecoflow.onQuota([](const String& serialNumber, JsonDocument& doc) {
    ecoflowDevices.applyQuota(serialNumber, doc);

    if (!ecoflowVerbose) {
      return;
    }

    // Схема повідомлення: {"id":..,"version":"1.0","timestamp":..,"params":{..}}
    // Набір ключів у params залежить від моделі, тому нічого не інтерпретуємо -
    // лише показуємо, що саме прийшло.
    JsonObjectConst params = doc["params"].as<JsonObjectConst>();
    const size_t count = params.isNull() ? 0 : params.size();
    _logger.info("quota %s params:%u", serialNumber.c_str(), static_cast<unsigned>(count));

    if (!ecoflowVerbose) {
      return;
    }

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

  // ...лише якщо auto-connect увімкнено: TLS-сесія коштує ~57 КБ heap (пряма)
  // чи менше (proxy), і на платі без PSRAM це може бути дорожче за саму
  // телеметрію.
  String autoConnectStored = configStorage.getString(CFG_ECOFLOW_AUTOCONNECT, "");
  static bool ecoflowAutoConnect =
      autoConnectStored.length() > 0 ? (autoConnectStored.toInt() != 0) : (ECOFLOW_AUTOCONNECT != 0);
  if (!ecoflowAutoConnect) {
    _logger.info("autoconnect is off - use 'ecoflow-start' to connect");
  }

  // Чи чекати на REST-знімок ПЕРЕД MQTT-конектом на старті (нижче), а чи
  // конектитись одразу. 'ecoflow-sync' лишається доступною командою в БУДЬ-
  // якому разі - це лише про порядок на старті, не про наявність REST узагалі.
  String syncBootStored = configStorage.getString(CFG_ECOFLOW_SYNC_BOOT, "");
  static bool ecoflowSyncOnBoot =
      syncBootStored.length() > 0 ? (syncBootStored.toInt() != 0) : (ECOFLOW_SYNC_ON_BOOT != 0);

  ecoflow.setRegistry(&ecoflowDevices);

  ecoflow.onAppCredentials([](const EcoflowMqttCredentials& credentials, const String& userId) {
    configStorage.setString(CFG_ECOFLOW_APP_ACCOUNT, credentials.certificateAccount);
    configStorage.setString(CFG_ECOFLOW_APP_PASSWORD, credentials.certificatePassword);
    configStorage.setString(CFG_ECOFLOW_APP_USER_ID, userId);
    _logger.info("saved to NVS");

    // Порівнюємо з тим, що зашито: якщо різне - на льоту не підхопиться.
    if (credentials.certificateAccount != String(ECOFLOW_MQTT_USERNAME)) {
      _logger.warn(
          "build-time account differs (%s) - put the values above into "
          "secrets.ini and reflash",
          ECOFLOW_MQTT_USERNAME);
    }
  });

  // Порядок навмисно REST -> MQTT, а не навпаки (раніше MQTT конектився
  // одразу на старті, а REST-знімок - через 30с після NTP, поверх уже
  // піднятого MQTT). REST-виклик EcoFlow (EcoflowAuthClient::signedGet)
  // піднімає ПРЯМИЙ TLS до api-e.ecoflow.com незалежно від того, чи MQTT іде
  // через proxy - і йому потрібен один суцільний блок (~57 КБ), який
  // найлегше знайти, поки MQTT ще нічого не займав. Тому спершу тягнемо
  // ПОВНИЙ знімок стану кожного пристрою (MQTT-quota шле лише дельти, і без
  // знімка наявність мережі лишається unknown до першої зміни).
  //
  // ecoflowSyncOnBoot=false пропускає це чекання цілком: MQTT конектиться
  // одразу, щойно з'явиться NTP+WiFi, без жодного REST-виклику. Блокуючим
  // для MQTT-конекту REST лишається лише КОЛИ увімкнений - вимкнути його
  // цілком (напр. плата з тісним heap, де навіть короткий REST - зайвий
  // ризик) не означає "MQTT теж не чекає нічого".
  static uint32_t ecoflowRestBaselineLargestBlock = 0;
  static bool ecoflowRestRequested = false;
  static bool ecoflowBootRecorded = false;
  static TaskId ecoflowAuditTaskId = 0;
  ecoflowAuditTaskId = scheduler.addCronTask(2 * 1000UL, []() {
    if (!ntp.isSynced() || !WiFi.isConnected()) {
      return;
    }

    // Пристрій "живий" лише тепер - NTP синхронізовано, WiFi піднято, час
    // придатний для запису. Раніше (setupEcoflow()) Boot писався одразу в
    // setup(), до NTP-синку - прапорець тут гарантує рівно один виклик за
    // фізичний старт, як і розраховано в EcoflowGridJournal::recordBoot().
    if (!ecoflowBootRecorded) {
      ecoflowDevices.recordBootForAll();
      ecoflowBootRecorded = true;
    }

    if (!ecoflowSyncOnBoot) {
      scheduler.removeTask(ecoflowAuditTaskId);
      if (ecoflowAutoConnect) {
        ecoflow.begin();
      }
      return;
    }

    if (!ecoflowRestRequested) {
      if (ecoflow.isBusy()) {
        return;
      }
      ecoflowRestBaselineLargestBlock = ESP.getMaxAllocHeap();
      _logger.info("REST snapshots: %u B free (largest block %u B) before requests", (unsigned)ESP.getFreeHeap(),
                   (unsigned)ecoflowRestBaselineLargestBlock);
      ecoflowRestRequested = true;
      ecoflow.syncSnapshotsAsync();
      return;
    }

    if (ecoflow.isBusy()) {
      return;
    }  // знімки ще тягнуться
    scheduler.removeTask(ecoflowAuditTaskId);

    const uint32_t largestAfter = ESP.getMaxAllocHeap();
    _logger.info("REST snapshots: %u B free (largest block %u B) after requests", (unsigned)ESP.getFreeHeap(),
                 (unsigned)largestAfter);

    if (!ecoflowAutoConnect) {
      return;
    }

    // НЕ "чи повернулись до baseline" (виміряного ДО жодної TLS-сесії за
    // весь uptime - він завжди значно вищий за усталений стан, і порівняння
    // з ним раз у раз блокувало автоконект навіть коли largest block цілком
    // достатній: заміряно 42996 B стабільно з прогону в прогін, не деградує
    // далі під повторними REST/heap-командами). Поріг - чи вистачає largest
    // block САМЕ на той конект, що зараз станеться: plain MQTT через proxy
    // не піднімає mbedTLS узагалі (тека tech_debt.md, "MQTT-проксі"), а прямий
    // TLS - ще один такий самий суцільний блок, що й щойно пішов на REST
    // (~57 КБ, там-таки заміряно).
    const uint32_t kMinLargestBlock = ecoflow.viaProxy() ? 12 * 1024 : 57 * 1024;
    if (largestAfter < kMinLargestBlock) {
      _logger.error(
          "largest block %u B too small for %s MQTT connect (need >= %u B) - "
          "autoconnect skipped, investigate before 'ecoflow-start'",
          (unsigned)largestAfter, ecoflow.viaProxy() ? "proxy" : "direct TLS", (unsigned)kMinLargestBlock);
      return;
    }

    ecoflow.begin();
  });

  const TaskId ecoflowShowCronTaskId = scheduler.addCronTask(60 * 1000UL, []() {
    if (!commandQueue.submit("ecoflow")) _logger.warn("command queue full, cron 'ecoflow' skipped");
  });

  // command: ecoflow
  commandHandler.registerCommand("ecoflow", "show EcoFlow cloud MQTT status", [](const String args) {
    char buf[20] = "";
    _logger.info("================= ECOFLOW ================= %s ================",
                 ntp.ftime("%Y-%m-%d %H:%M:%S", buf, sizeof(buf)));
    _logger.debug("connected = %s, account = %s", ecoflow.isConnected() ? "yes" : "no", ecoflow.account().c_str());
    _logger.debug("broker = %s:%d (%s), channel = %s, verbose = %s", ecoflow.brokerHost(), ecoflow.brokerPort(),
                  ecoflow.viaProxy() ? "proxy" : "tls", EcoflowClient::channelName(ecoflow.channel()),
                  ecoflowVerbose ? "on" : "off");
    // TLS-сесія - найбільший споживач heap у цьому клієнті, тому цифри тут
    // корисніші за загальний 'dump-heap': саме вони кажуть, чи пройде REST.
    _logger.debug("running = %s, heap = %u B free, largest block = %u B", ecoflow.isRunning() ? "yes" : "no",
                  (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());
    // Запас стеку мережевого таска: підстава змінювати MqttConfig::taskStackSize.
    _logger.debug("net task stack headroom = %u B", (unsigned)ecoflow.networkStackHeadroom());
    if (ecoflow.lastError().length() > 0) {
      _logger.warn("last error: %s", ecoflow.lastError().c_str());
    }

    _logger.debug("messages received = %u, last topic = %s", ecoflow.messageCount(),
                  ecoflow.lastTopic().length() > 0 ? ecoflow.lastTopic().c_str() : "(none)");

    const char* separator = "-------------------------------";

    _logger.debug("");
    _logger.info("%-1s %-16s %-18s %-7s %6s %-9s %7s %9s", "#", "SERIAL", "NAME", "STATUS", "CHARGE", "GRID", "LEFT",
                 "AGE");
    _logger.info("%.1s %.16s %.18s %.7s %.6s %.9s %.7s %.9s", separator, separator, separator, separator, separator,
                 separator, separator, separator);
    size_t deviceIndex = 0;
    for (const auto& state : ecoflowDevices.devices()) {
      // Той самий індекс, що приймають 'ecoflow-params' і 'ecoflow-capture'.
      // CHARGE і TIME вирівняні вправо, щоб числа читались колонкою.
      // Три стани, а не два: REST-знімок заповнює CHARGE/TIME, але НЕ доводить,
      // що пристрій живий - раніше це виглядало як "offline із свіжими даними".
      // lastMessageMs == 0 означає саме "жодного повідомлення ще не чули".
      const char* presence = state.lastMessageMs == 0 ? "unknown" : (state.online ? "online" : "offline");

      // Без ведучих пробілів: колонку вирівнює сам printf ("%6s").
      char charge[8] = "-";
      if (state.hasSoc()) {
        snprintf(charge, sizeof(charge), "%d%%", (int)state.socPercent);
      }

      // TIME стоїть ПІСЛЯ GRID: EcoFlow віддає одне поле remainTime і на заряд,
      // і на розряд, тому саме сусідство з GRID і пояснює, що воно означає.
      const String remaining = EcoflowDeviceRegistry::formatRemainTime(state.remainTimeMinutes);

      // "Скільки триває поточний стан" - з журналу (EcoflowGridJournal,
      // переживає ребут), не з RAM-only мітки - тієї більше немає
      // (tech_debt.md, "не дублювати previousGrid").
      String gridFor = "-";
      const EcoflowGridJournal* gridJournal = ecoflowDevices.journalAt(state.journalIndex);
      const time_t gridSince = gridJournal != nullptr ? gridJournal->gridSinceEpoch() : 0;
      const time_t nowEpoch = time(nullptr);
      if (gridSince != 0 && nowEpoch > gridSince) {
        gridFor = EcoflowDeviceRegistry::formatDurationSeconds((uint32_t)(nowEpoch - gridSince));
      }
      //                1    2     3     4    5   6    7   8
      _logger.info("%-1u %-16s %-18s %-7s %6s %-9s %7s %9s",
                   (unsigned)deviceIndex++,   // 1
                   state.info->serialNumber,  // 2
                   state.info->name,          // 3
                   presence, charge,
                   state.gridInferred ? (String(ecoflowGridStateName(state.grid)) + "*").c_str()
                                      : ecoflowGridStateName(state.grid),
                   remaining.c_str(), gridFor.c_str());
    }
    _logger.info("");
  });

  // command: ecoflow-watch
  commandHandler.registerCommand("ecoflow-watch", "periodic EcoFlow device table: ecoflow-watch [on|off]",
                                 [ecoflowShowCronTaskId](const String args) {
                                   // Порожній аргумент - лише звіт: раніше команда була перемикачем, і
                                   // «подивитись стан» мовчки міняло його на протилежний.
                                   if (args.equalsIgnoreCase("on")) {
                                     scheduler.resume(ecoflowShowCronTaskId);
                                     configStorage.setBool(CFG_ECOFLOW_WATCH, true);
                                   } else if (args.equalsIgnoreCase("off")) {
                                     scheduler.pause(ecoflowShowCronTaskId);
                                     configStorage.setBool(CFG_ECOFLOW_WATCH, false);
                                   } else if (args.length() != 0) {
                                     _logger.info("use: ecoflow-watch [on|off]");
                                     return;
                                   }
                                   _logger.info("ecoflow watch - %s",
                                                scheduler.isPaused(ecoflowShowCronTaskId) ? "off" : "on");
                                 });

  // enable/disable periodic ecoflow device table
  if (!configStorage.getBool(CFG_ECOFLOW_WATCH, true)) {
    scheduler.pause(ecoflowShowCronTaskId);
    _logger.warn("ecoflow watch disabled");
  }

  // Обидві REST-команди йдуть у власний таск: TLS-хендшейк не вміщується
  // комфортно в стек головного loopTask, а пауза MQTT на час запиту заблокувала
  // б sketch loop() на кілька секунд (дисплей/тач завмирали б).
  // command: ecoflow-devices
  commandHandler.registerCommand("ecoflow-devices", "fetch EcoFlow device list over REST (async, result in log)",
                                 [](const String args) {
                                   if (!ecoflow.refreshDevicesAsync()) {
                                     _logger.error("not started: %s", ecoflow.lastError().c_str());
                                     return;
                                   }
                                   _logger.info("request sent, result will appear in the log (MQTT suspended ~2-5 s)");
                                 });

  // command: ecoflow-login
  commandHandler.registerCommand("ecoflow-login",
                                 "log in to EcoFlow private app API (email+password) -> MQTT credentials + userId",
                                 [](const String args) {
                                   if (!ecoflow.issueAppCredentialsAsync()) {
                                     _logger.error("not started: %s", ecoflow.lastError().c_str());
                                     return;
                                   }
                                   _logger.info("logging in, result will appear in the log (MQTT paused)");
                                 });

  // command: ecoflow-capture
  commandHandler.registerCommand(
      "ecoflow-capture", "capture ALL params: ecoflow-capture <on|off> [sn|index|all]", [](const String args) {
        String rest = args;
        rest.trim();
        const int space = rest.indexOf(' ');
        String mode = space < 0 ? rest : rest.substring(0, space);
        String target = space < 0 ? String("all") : rest.substring(space + 1);
        mode.trim();
        target.trim();

        if (mode.length() == 0) {
          size_t i = 0;
          for (const auto& state : ecoflowDevices.devices()) {
            _logger.info("  %u  %-16s %-18s capture=%-3s params=%u%s", (unsigned)i++, state.info->serialNumber,
                         state.info->name, state.captureAll ? "all" : "imp", (unsigned)state.trackedParams.size(),
                         state.droppedParams ? "  (limit reached)" : "");
          }
          _logger.info("use: ecoflow-capture <on|off> [sn|index|all]");
          _logger.info("  on|off - capture every received param instead of only whitelisted ones");
          _logger.info("  target: 'all' (default) - apply to every device");
          _logger.info("          <index>          - device position in the list above");
          _logger.info("          <sn>             - device serial number");
          return;
        }

        bool enable = false;
        if (!parseBool(mode, enable)) {
          _logger.warn("use: ecoflow-capture <on|off> [sn|index|all]");
          return;
        }
        // Порожній serial у setCaptureAll() означає "усі пристрої".
        String serial;
        if (target.length() > 0 && target != "all") {
          String error;
          serial = ecoflowSerialFromKey(ecoflowDevices, target, &error);
          if (serial.length() == 0) {
            _logger.error("%s", error.c_str());
            return;
          }
        }
        const size_t affected = ecoflowDevices.setCaptureAll(serial, enable);
        _logger.info("capture all = %s for %u device(s)", enable ? "on" : "off", (unsigned)affected);
      });

  // command: ecoflow-journal
  //
  // Мінімальна версія контракту (docs/tech_debt.md, розділ 8 - кандидат на
  // розширення: clear/reset, dump <file> <sn|index|all> у LittleFS/SD,
  // налаштування алертів - свідомо НЕ тут).
  commandHandler.registerCommand(
      "ecoflow-journal", "grid transitions journal: ecoflow-journal <on|off|show> [sn|index|all]",
      [](const String args) {
        String rest = args;
        rest.trim();
        const int space = rest.indexOf(' ');
        String mode = space < 0 ? rest : rest.substring(0, space);
        String target = space < 0 ? String("all") : rest.substring(space + 1);
        mode.trim();
        target.trim();

        if (mode == "on" || mode == "off") {
          const bool enable = (mode == "on");
          ecoflowDevices.setJournalPersistenceEnabledForAll(enable);
          configStorage.setString(CFG_ECOFLOW_JOURNAL, enable ? "1" : "0");
          _logger.info("journal NVS writes = %s", enable ? "on" : "off");
          return;
        }

        if (mode != "show") {
          // Розгорнутий help (не одна лаконічна "use:"-стрічка, як в інших
          // ecoflow-команд): аргументи тут не самопояснювальні (on/off керує
          // лише NVS-частиною, а mark/AGE у show - не очевидні без опису),
          // тому один рядок "use:" лишав би людину гортати docs/ecoflow.md.
          _logger.info("use: ecoflow-journal <on|off|show> [sn|index|all]");
          _logger.info("  on|off        - enable/disable journal writes to NVS (MQTT mirror keeps working either way)");
          _logger.info("  show [target] - print merged Transition history, oldest -> newest, up to %u rows/device",
                       (unsigned)kEcoflowJournalShowLimit);
          _logger.info("    target: 'all' (default) - merge every device into one chronological stream");
          _logger.info("            <index>          - device position, see 'ecoflow-params' with no argument");
          _logger.info("            <sn>             - device serial number");
          _logger.info("  columns: DATE/TIME  DEVICE  GRID  mark  AGE");
          _logger.info("    mark: '>' newest row for that device, state still ongoing (AGE keeps growing)");
          _logger.info("          '<' last fully closed interval for that device (AGE is final)");
          _logger.info("          ' ' older history");
          _logger.info("    AGE:  time spent in that GRID state, forward to the device's next transition (or now)");
          _logger.info("  Boot/LiveCheckpoint events are not shown here - NVS/MQTT-mirror/web portal only");
          return;
        }

        // Побудова списку - спільна з HTTP-роутом /api/ecoflow/journal і SAPI-
        // командою 'ecoflow-journal' (EcoflowJournalView.hpp, DRY - CLAUDE.md).
        std::vector<EcoflowJournalRow> rows;
        String error;
        if (!ecoflowBuildJournalRows(ecoflowDevices, target, rows, &error)) {
          _logger.error("%s", error.c_str());
          return;
        }

        if (rows.empty()) {
          _logger.info("journal is empty");
          return;
        }

        constexpr int kDeviceNameWidth = 18;  // "DELTA Pro (xama)" (16) + запас
        constexpr int kGridWidth = 9;         // "off-grid" (8) + запас
        // Колонка перед AGE - без заголовка навмисно (Ecoflow/EcoflowJournalView.cpp,
        // appendJournalRows(), поле "mark"): сама її наявність у рядку вже
        // підказка, підпис лише заважав би.
        _logger.info("%-19s  %-*s  %-*s    %9s", "DATE/TIME", kDeviceNameWidth, "DEVICE", kGridWidth, "GRID", "AGE");
        for (const auto& row : rows) {
          char timestamp[32] = "";
          struct timeval tv{row.atEpoch, 0};
          ntp.ftime("%Y-%m-%d %H:%M:%S", timestamp, sizeof(timestamp), &tv);
          const String age = EcoflowDeviceRegistry::formatDurationSeconds(row.ageSec);
          _logger.info("%-19s  %-*s  %-*s  %c %9s", timestamp, kDeviceNameWidth, row.deviceName, kGridWidth,
                       ecoflowGridStateName(row.toState), row.mark, age.c_str());
        }
      });

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
          _logger.info("use: ecoflow-params <sn|index> [pattern, e.g. *_in_*]");
          _logger.info("  target: <index> - device position in the list below");
          _logger.info("          <sn>    - device serial number");
          _logger.info("  pattern (optional) - glob filter on the normalized param key, '*' wildcard");
          size_t i = 0;
          for (const auto& state : ecoflowDevices.devices()) {
            _logger.info("  %u  %-16s %-18s capture=%s params=%u", (unsigned)i++, state.info->serialNumber,
                         state.info->name, state.captureAll ? "all" : "imp", (unsigned)state.trackedParams.size());
          }
          return;
        }

        String error;
        const String serial = ecoflowSerialFromKey(ecoflowDevices, key, &error);
        if (serial.length() == 0) {
          _logger.error("%s", error.c_str());
          return;
        }

        // Друкуємо ЗАХОПЛЕНЕ, а не свіжий REST-запит: так команда миттєва і не
        // рве MQTT-сесію. Щоб підтягти повний стан з хмари - 'ecoflow-sync'.
        for (const auto& state : ecoflowDevices.devices()) {
          if (serial != state.info->serialNumber) {
            continue;
          }
          _logger.info("%s (%s): %u params, capture=%s%s", state.info->name, state.info->serialNumber,
                       (unsigned)state.trackedParams.size(), state.captureAll ? "all" : "important-only",
                       state.snapshotAvailable ? "" : ", MQTT-only");
          if (state.droppedParams > 0) {
            _logger.warn("  %u more keys dropped - per-device limit reached", (unsigned)state.droppedParams);
          }

          // std::map уже впорядкований лексикографічно, тому однакові префікси
          // ('inv_*', 'pd_*') виводяться згрупованими без окремого сортування.
          size_t shown = 0;
          for (const auto& kv : state.trackedParams) {
            if (pattern.length() > 0 && !EcoflowDeviceRegistry::wildcardMatch(pattern.c_str(), kv.first.c_str())) {
              continue;
            }
            _logger.info("  %-34s = %.3f", kv.first.c_str(), kv.second);
            shown++;
          }
          if (pattern.length() > 0) {
            _logger.info("  %u of %u params match '%s'", (unsigned)shown, (unsigned)state.trackedParams.size(),
                         pattern.c_str());
          }

          // Порожній результат майже завжди означає одне з трьох - підказуємо, що
          // саме, бо інакше виглядає як "пристрій цього не шле".
          if (shown == 0 && pattern.length() > 0) {
            char normalized[EcoflowDeviceRegistry::kMaxKeyLength];
            EcoflowDeviceRegistry::normalizeKey(pattern.c_str(), normalized, sizeof(normalized));
            if (EcoflowDeviceRegistry::isBlacklistedParam(normalized)) {
              _logger.info("  reason: key is blacklisted");
            } else if (!state.captureAll && !EcoflowDeviceRegistry::isWhitelistedParam(normalized)) {
              _logger.info("  reason: not whitelisted; try 'ecoflow-capture on %s'", state.info->serialNumber);
            } else if (pattern.indexOf('*') < 0) {
              _logger.info("  reason: not received yet (quota sends only changed fields)");
              _logger.info("  hint: keys are normalized - try '*%s*'", normalized);
            }
          }
          return;
        }
      });

  // command: ecoflow-sync
  commandHandler.registerCommand(
      "ecoflow-sync",
      "REST snapshot: ecoflow-sync <on|off|all|sn|index> - on/off toggles sync-before-MQTT "
      "on boot, all/sn/index pulls a snapshot now",
      [](const String args) {
        String value = args;
        value.trim();

        if (value.length() == 0) {
          String stored = configStorage.getString(CFG_ECOFLOW_SYNC_BOOT, "");
          _logger.info("use: ecoflow-sync <on|off|all|sn|index>");
          _logger.info("sync-on-boot = %s%s",
                       stored.length() > 0 ? (stored.toInt() ? "on" : "off") : (ECOFLOW_SYNC_ON_BOOT ? "on" : "off"),
                       stored.length() > 0 ? "" : " (build-time default)");
          return;
        }

        if (value == "on" || value == "off") {
          const bool on = (value == "on");
          configStorage.setString(CFG_ECOFLOW_SYNC_BOOT, on ? "1" : "0");
          _logger.info("sync-on-boot = %s (applies on next boot)", on ? "on" : "off");
          return;
        }

        // Порожній serial у syncSnapshotsAsync() означає "усі пристрої" - той
        // самий контракт, що й setCaptureAll() (ecoflow-capture).
        String serial;
        if (value != "all") {
          String error;
          serial = ecoflowSerialFromKey(ecoflowDevices, value, &error);
          if (serial.length() == 0) {
            _logger.error("%s", error.c_str());
            return;
          }
        }

        if (!ecoflow.syncSnapshotsAsync(serial)) {
          _logger.error("not started: %s", ecoflow.lastError().c_str());
          return;
        }
        _logger.info("snapshot sync started, result will appear in the log");
      });

  // // command: ecoflow-start
  commandHandler.registerCommand("ecoflow-start", "connect EcoFlow cloud MQTT (frees nothing, costs ~57 KB heap)",
                                 [](const String args) {
                                   if (!ecoflow.start()) {
                                     _logger.error("start failed: %s", ecoflow.lastError().c_str());
                                     return;
                                   }
                                   _logger.info("running = %s", ecoflow.isRunning() ? "yes" : "no");
                                 });

  // command: ecoflow-stop
  commandHandler.registerCommand("ecoflow-stop", "drop EcoFlow cloud MQTT and free its TLS session (~57 KB)",
                                 [](const String args) {
                                   if (!ecoflow.stop()) {
                                     _logger.error("stop failed: %s", ecoflow.lastError().c_str());
                                   }
                                 });

  // command: ecoflow-auto
  commandHandler.registerCommand(
      "ecoflow-auto", "connect EcoFlow on boot: ecoflow-auto [on|off]", [](const String args) {
        String value = args;
        value.trim();
        if (value.length() == 0) {
          String stored = configStorage.getString(CFG_ECOFLOW_AUTOCONNECT, "");
          _logger.info("autoconnect = %s%s",
                       stored.length() > 0 ? (stored.toInt() ? "on" : "off") : (ECOFLOW_AUTOCONNECT ? "on" : "off"),
                       stored.length() > 0 ? "" : " (build-time default)");
          return;
        }
        bool on = false;
        if (!parseBool(value, on)) {
          _logger.warn("use: ecoflow-auto [on|off]");
          return;
        }
        configStorage.setString(CFG_ECOFLOW_AUTOCONNECT, on ? "1" : "0");
        _logger.info("autoconnect = %s (applies on next boot)", on ? "on" : "off");
      });

  // command: ecoflow-verbose
  commandHandler.registerCommand("ecoflow-verbose", "toggle EcoFlow telemetry dump: ecoflow-verbose [on|off]",
                                 [](const String args) {
                                   String value = args;
                                   value.trim();
                                   bool on = !ecoflowVerbose;
                                   if (value.length() > 0 && !parseBool(value, on)) {
                                     _logger.warn("use: ecoflow-verbose [on|off]");
                                     return;
                                   }
                                   ecoflowVerbose = on;
                                   _logger.info("verbose = %s", ecoflowVerbose ? "on" : "off");
                                 });

  // command: ecoflow-cert
  commandHandler.registerCommand("ecoflow-cert",
                                 "re-issue MQTT credentials via EcoFlow Open Platform API (accessKey/secretKey)",
                                 [](const String args) {
                                   if (!ecoflow.refreshCredentialsAsync()) {
                                     _logger.error("not started: %s", ecoflow.lastError().c_str());
                                     return;
                                   }
                                   _logger.info("request sent, result will appear in the log (MQTT suspended ~2-5 s)");
                                 });
}

#endif
