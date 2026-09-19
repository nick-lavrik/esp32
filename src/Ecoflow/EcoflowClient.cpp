#include "EcoflowClient.hpp"

#include <TLogger.hpp>
#include <Trace.hpp>
#include <WiFi.h>

#include "EcoflowAppAuthClient.hpp"
#include "EcoflowDeviceRegistry.hpp"
#include "EcoflowMqttTopics.hpp"

namespace {
TLogger logger{"ecoflow"};
}

EcoflowClient::Channel EcoflowClient::channelFromAccount(const String &account) {
  return account.startsWith("app-") ? Channel::AppPrivate : Channel::OpenPlatform;
}

const char *EcoflowClient::channelName(Channel channel) {
  return channel == Channel::AppPrivate ? "app (private API)" : "open (Open Platform)";
}

std::string EcoflowClient::buildRootTopic(const char *account) {
  // Один root-топік на весь акаунт: далі MqttClient сам роздає повідомлення по
  // фільтрах addListener(). Глобальне "#" тут заборонене ACL-ом брокера.
  if (account == nullptr || account[0] == '\0') {
    return std::string();
  }
  // Локальний фільтр-диспетчер PicoMQTT (брокеру не надсилається), тому
  // wildcard тут безпечний - див. коментар у begin().
  if (channelFromAccount(String(account)) == Channel::AppPrivate) {
    return std::string("/app/device/property/#");
  }
  return std::string("/open/") + account + "/#";
}

String EcoflowClient::buildClientId(const Config &config) {
  const String account = config.mqttUsername != nullptr ? String(config.mqttUsername) : String();
  if (channelFromAccount(account) != Channel::AppPrivate) {
    return config.clientId != nullptr ? String(config.clientId) : String();
  }

  // Перевірено на живому брокері: обовʼязкові саме префікс "ANDROID_" і
  // справжній userId у кінці. Середина довільна (беремо clientId проєкту, щоб
  // з'єднання було вузнаваним), "IOS_" і чужий userId дають
  // "Connection Refused: not authorised".
  const String middle = config.clientId != nullptr ? String(config.clientId) : String("esp32");
  const String userId = config.userId != nullptr ? String(config.userId) : String();
  return "ANDROID_" + middle + "_" + userId;
}

MqttConfig EcoflowClient::makeMqttConfig(const Config &config, const std::string &rootTopic,
                                        const String &clientId) {
  MqttConfig mqttConfig;
  mqttConfig.clientId = clientId.c_str();

  if (config.proxyHost != nullptr) {
    // Стадія 1 MQTT-проксі (docs/tech_debt.md): TLS уже знято проксі,
    // сюди приходить plain MQTT довіреною LAN. ЛОКАЛЬНІ креденшли, не
    // облікові дані EcoFlow (mqttUsername/mqttPassword тут НЕ йдуть на
    // дріт - вони й далі лише визначають канал/схему топіків вище).
    mqttConfig.host = config.proxyHost;
    mqttConfig.port = config.proxyPort;
    mqttConfig.useTls = false;
    mqttConfig.useAuth = config.proxyUsername != nullptr;
    mqttConfig.username = config.proxyUsername;
    mqttConfig.password = config.proxyPassword;
    // Без mbedTLS-хендшейку 8 КБ (дефолт MqttConfig) вистачає з запасом -
    // немає сенсу тримати ті самі 16 КБ, що й для TLS-шляху нижче.
    mqttConfig.taskName = "ecoflow-proxy-net";
  } else {
    mqttConfig.host = config.mqttHost;
    mqttConfig.port = config.mqttPort;
    mqttConfig.useAuth = true;
    mqttConfig.username = config.mqttUsername;
    mqttConfig.password = config.mqttPassword;

    // Брокер EcoFlow приймає лише mqtts. caCert не задаємо -> setInsecure();
    // TODO(production): закріпити CA-сертифікат, як і в EcoflowAuthClient.
    mqttConfig.useTls = true;
    mqttConfig.caCert = nullptr;

    // 16 КБ. Спроба зрізати до 10 КБ (за заміром headroom 12 480 B з 16 384, тобто
    // пік ~3.9 КБ) закінчилась ЗАВИСАННЯМ плати без panic-логу. Причина, найпевніше,
    // у шляху, якого замір не покривав: 'DNS Failed' -> 'Connect fail' -> повторний
    // хендшейк, де стек глибший за стабільну сесію. 6 КБ економії не варті цього -
    // тим більше що RGB332-фон уже звільнив ~55 КБ.
    mqttConfig.taskStackSize = 16 * 1024;
    mqttConfig.taskName = "ecoflow-net";
  }

  // Топіки EcoFlow передаються брокеру байт-у-байт разом із провідним '/'
  // (MqttKeyGenerator його зрізав би - див. MqttConfig::useKeyGenerator).
  mqttConfig.useKeyGenerator = false;
  mqttConfig.rootSubscribeTopic = rootTopic.c_str();

  // quota великих станцій перевищує дефолтні 2 КБ, але 8 КБ виявились із
  // запасом: найбільше реально бачене повідомлення ~2 КБ. MQTT і так не
  // гарантує доставку, тож обрізаний викид - не втрата даних.
  mqttConfig.rootSubscribeBufferSize = 4 * 1024;

  // LWT не задаємо: брокер EcoFlow не дозволяє публікацію в довільні топіки.

  return mqttConfig;
}

EcoflowClient::EcoflowClient(const Config &config)
    : _config(config),
      _account(config.mqttUsername != nullptr ? config.mqttUsername : ""),
      _channel(channelFromAccount(config.mqttUsername != nullptr ? String(config.mqttUsername)
                                                                 : String())),
      _clientIdStorage(buildClientId(config)),
      _rootTopicStorage(buildRootTopic(config.mqttUsername)),
      _mqtt(makeMqttConfig(config, _rootTopicStorage, _clientIdStorage)),
      _auth(config.accessKey != nullptr ? config.accessKey : "",
            config.secretKey != nullptr ? config.secretKey : "") {
}

String EcoflowClient::serialFromTopic(const char *topic) {
  // Приватний канал: "/app/device/property/{sn}" - sn останній сегмент.
  const String value(topic != nullptr ? topic : "");
  if (value.startsWith("/app/device/property/")) {
    return value.substring(value.lastIndexOf('/') + 1);
  }

  // "/open/{account}/{sn}/quota" -> {sn} - це третій сегмент (перший порожній
  // через провідний '/').
  int accountStart = value.indexOf('/', 1);  // кінець "/open"
  if (accountStart < 0) {
    return String();
  }
  int snStart = value.indexOf('/', accountStart + 1);
  if (snStart < 0) {
    return String();
  }
  int snEnd = value.indexOf('/', snStart + 1);
  if (snEnd < 0) {
    return String();
  }
  return value.substring(snStart + 1, snEnd);
}

void EcoflowClient::begin() {
  if (_started) {
    return;
  }

  if (_account.length() == 0) {
    _lastError = "mqttUsername (certificateAccount) not set";
    logger.error("%s", _lastError.c_str());
    return;
  }

  // Серійні номери беремо з ПРОШИТОГО переліку (EcoflowDeviceRegistry), а не з
  // REST: підпис REST-запиту містить timestamp, тому на старті (до NTP) він
  // приречений, а без sn не побудувати жодної підписки. Тепер MQTT піднімається
  // одразу після WiFi, а REST лишається необов'язковою звіркою з хмарою
  // ('ecoflow-devices').
  //
  // ACL EcoFlow приймає ЛИШЕ точні топіки: перевірено, що відхиляються і
  // "/open/{account}/#", і "/open/{account}/+/quota", і навіть
  // "/open/{account}/{sn}/#". Тому підписка - окрема на кожен sn.
  //
  // Wildcard тут лишається тільки в MqttConfig::rootSubscribeTopic, і це
  // безпечно: PicoMQTT реєструє його через SubscribedMessageListener, тобто
  // ЛОКАЛЬНО, як фільтр-диспетчер вхідних повідомлень - брокеру він не
  // надсилається. Брокер бачить лише ті топіки, що йдуть через addListener().
  const EcoflowDeviceInfo *table = EcoflowDeviceRegistry::deviceTable();
  for (size_t i = 0; i < EcoflowDeviceRegistry::deviceCount(); i++) {
    const String serial = table[i].serialNumber;
    logger.debug("%-16s %s", table[i].serialNumber, table[i].name);

    auto quotaHandler = [this](const char *topic, JsonDocument &doc) {
      _messageCount++;
      _lastTopic = topic;
      if (_quotaCallback) {
        _quotaCallback(serialFromTopic(topic), doc);
      }
    };

    if (_channel == Channel::AppPrivate) {
      // Приватний канал: один топік на пристрій, і в ньому вже все. Окремого /status немає - онлайн
      // визначається тишею (EcoflowDeviceRegistry::expireStale), як і для пристроїв, що /status не шлють.
      _mqtt.addJsonListener(EcoflowMqttTopics::appProperty(serial).c_str(), quotaHandler);
      continue;
    }

    _mqtt.addJsonListener(EcoflowMqttTopics::quota(_account, serial).c_str(), quotaHandler);
    _mqtt.addJsonListener(
      EcoflowMqttTopics::status(_account, serial).c_str(),
      [this](const char *topic, JsonDocument &doc) {
        _messageCount++;
        _lastTopic = topic;
        if (_statusCallback) {
          _statusCallback(serialFromTopic(topic), doc);
        }
      }
    );
  }

  _mqtt.begin();
  _started = true;
  logger.info("%s:%u account=%s, subscriptions: %u", _config.mqttHost, (unsigned)_config.mqttPort,
              _account.c_str(), (unsigned)(EcoflowDeviceRegistry::deviceCount() * 2));
}

void EcoflowClient::loop() {
  _mqtt.loop();

  if (_mqttResumePending && !_restBusy) {
    _mqttResumePending = false;
    _mqtt.resume();
  }
}

// Стек REST-таска: TLS-хендшейк (mbedTLS RSA/ECDHE) + HTTPClient + розбір JSON.
// 16 КБ - той самий порядок, що й у мережевого таска MQTT з TLS.
// 16 КБ. Замір показував запас 12 764 B з 16 384 (тобто пік ~3.6 КБ), але після
// того, як така сама «обґрунтована» економія на мережевому таску підвісила плату,
// тут теж лишаємо запас: TLS-шлях має рідкі глибші гілки (retry, інший cipher).
static constexpr uint32_t kRestTaskStackSize = 16 * 1024;

struct EcoflowRestTaskArg {
  EcoflowClient *self;
  int job;
  String serialNumber;  // лише для kSnapshots: порожній = усі пристрої
};

void EcoflowClient::restTaskTrampoline(void *param) {
  auto *arg = static_cast<EcoflowRestTaskArg *>(param);
  EcoflowClient *self = arg->self;
  const RestJob job = static_cast<RestJob>(arg->job);
  const String serialNumber = arg->serialNumber;
  delete arg;

  self->runRestJob(job, serialNumber);

  // Скільки стеку лишилось невикористаним - якщо тут близько до нуля,
  // kRestTaskStackSize треба піднімати.
  logger.debug("rest task finished, stack headroom %u B",
               (unsigned)(uxTaskGetStackHighWaterMark(nullptr) * sizeof(StackType_t)));

  self->_restBusy = false;
  vTaskDelete(nullptr);
}

void EcoflowClient::runRestJob(RestJob job, const String &serialNumber) {
  if (job == RestJob::kStart) {
    begin();
    return;
  }

  if (job == RestJob::kAppLogin) {
    if (_config.email == nullptr || _config.emailPassword == nullptr) {
      logger.error("email/password not set - cannot issue app credentials");
      return;
    }

    withMqttSuspended("app-login", [this]() {
      EcoflowAppAuthClient appAuth(_config.email, _config.emailPassword);
      EcoflowMqttCredentials credentials;
      if (!appAuth.fetchMqttCredentials(credentials)) {
        logger.error("app login failed: %s", appAuth.lastError().c_str());
        return false;
      }

      // Друкуємо повністю: сенс команди - перенести значення в secrets.ini
      // (або звірити з тим, що вже збережено в NVS).
      logger.info("url=%s port=%u protocol=%s", credentials.url.c_str(),
                  (unsigned)credentials.port, credentials.protocol.c_str());
      logger.info("account  = %s", credentials.certificateAccount.c_str());
      logger.info("password = %s", credentials.certificatePassword.c_str());
      logger.info("user id  = %s  <- потрібен для clientId ANDROID_..._<userId>",
                  appAuth.userId().c_str());
      if (_appCredentialsCallback) {
        _appCredentialsCallback(credentials, appAuth.userId());
      }
      return true;
    });
    return;
  }

  if (job == RestJob::kSnapshots) {
    if (_registry == nullptr) {
      logger.error("no registry attached - snapshots have nowhere to go");
      return;
    }
    // Один suspend на ВСІ пристрої: кожен окремий коштував би розриву й
    // підняття TLS-сесії (~57 КБ і кілька секунд). Те саме - і на один
    // пристрій (serialNumber непорожній): MQTT все одно доведеться
    // призупинити на час запиту.
    withMqttSuspended("snapshots", [this, serialNumber]() {
      logger.info("snapshots: starting, %u B free (largest block %u B)", (unsigned)ESP.getFreeHeap(),
                  (unsigned)ESP.getMaxAllocHeap());
      size_t ok = 0, skipped = 0;
      bool first = true;
      for (const auto &state : _registry->devices()) {
        if (!serialNumber.isEmpty() && serialNumber != state.info->serialNumber) { continue; }
        if (!state.snapshotAvailable) { skipped++; continue; }

        // Невеликий інтервал між запитами: попередній TLS-клієнт (локальний
        // у signedGet()) встигає розібратись і повернути свій блок у купу до
        // старту наступного хендшейку.
        if (!first) { delay(300); }
        first = false;

        const String sn = state.info->serialNumber;
        JsonDocument doc;
        bool notAllowed = false;
        bool fetched = false;
        // withTrace() (lib/Logger/Trace.hpp) - той самий замір "тривалість +
        // heap до/після", що й на кожен setupXxx() при старті, а не свій
        // окремий millis()/ESP.getFreeHeap() тут. Largest block per-пристрій
        // уже перевірено окремо (не накопичується від пристрою до пристрою,
        // просідає раз на весь suspend) - далі досить сумарного "starting/
        // when done" вище й нижче.
        withTrace(state.info->name, [&]() { fetched = _auth.fetchQuotaAll(sn, doc, notAllowed); });
        if (fetched) {
          if (_registry->applySnapshot(sn, doc)) { ok++; }
        } else if (notAllowed) {
          // Постійна властивість пристрою, не збій: більше не питаємо.
          _registry->markSnapshotUnavailable(sn);
          logger.info("%s: REST snapshot not permitted, MQTT-only", state.info->name);
          skipped++;
        } else {
          logger.warn("%s: snapshot failed: %s, WiFi status=%d RSSI=%d dBm, %u B free (largest block %u B)",
                      state.info->name, _auth.lastError().c_str(), (int)WiFi.status(), (int)WiFi.RSSI(),
                      (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());
        }
      }
      logger.info("snapshots: %u applied, %u skipped, %u B free (largest block %u B) when done",
                  (unsigned)ok, (unsigned)skipped, (unsigned)ESP.getFreeHeap(),
                  (unsigned)ESP.getMaxAllocHeap());
      return true;
    });
    return;
  }

  if (job == RestJob::kDevices) {
    if (!refreshDevices()) {
      logger.error("device list fail: %s", _lastError.c_str());
      return;
    }
    logger.info("fetched %u devices:", (unsigned)_devices.size());
    for (const auto &device : _devices) {
      logger.info("  %s %-22s %s", device.serialNumber.c_str(), device.name.c_str(),
                  device.online ? "online" : "offline");
    }
    logger.info("new devices are picked up at startup only - reboot required");
    return;
  }

  EcoflowMqttCredentials credentials;
  if (!refreshCredentials(credentials)) {
    logger.error("certification fail: %s", _lastError.c_str());
    return;
  }
  // Свідомо друкуємо повністю: сенс виклику - перенести значення в secrets.ini.
  logger.info("url=%s port=%u protocol=%s", credentials.url.c_str(), (unsigned)credentials.port,
              credentials.protocol.c_str());
  logger.info("account=%s password=%s", credentials.certificateAccount.c_str(),
              credentials.certificatePassword.c_str());
}

bool EcoflowClient::startRestTask(RestJob job, const String &serialNumber) {
  if (_restBusy) {
    _lastError = "a REST request is already running";
    return false;
  }
  _restBusy = true;

  auto *arg = new EcoflowRestTaskArg{this, static_cast<int>(job), serialNumber};
  // xTaskCreate без пінінгу - ESP32-C6 single-core (див. MqttClient::begin()).
  if (xTaskCreate(&EcoflowClient::restTaskTrampoline, "ecoflow-rest", kRestTaskStackSize, arg,
                  tskIDLE_PRIORITY + 1, nullptr) != pdPASS) {
    delete arg;
    _restBusy = false;
    _lastError = "failed to create rest task (out of memory?)";
    return false;
  }
  return true;
}

bool EcoflowClient::beginAsync() { return startRestTask(RestJob::kStart); }

bool EcoflowClient::refreshDevicesAsync() { return startRestTask(RestJob::kDevices); }

bool EcoflowClient::syncSnapshotsAsync(const String &serialNumber) {
  return startRestTask(RestJob::kSnapshots, serialNumber);
}

bool EcoflowClient::issueAppCredentialsAsync() { return startRestTask(RestJob::kAppLogin); }

bool EcoflowClient::refreshCredentialsAsync() { return startRestTask(RestJob::kCredentials); }

bool EcoflowClient::stop() {
  if (!_started) {
    _lastError = "EcoFlow not started";
    return false;
  }
  if (_mqtt.isSuspended()) {
    return true;  // вже зупинений - не помилка
  }
  if (!_mqtt.suspend()) {
    _lastError = "failed to suspend MQTT";
    return false;
  }
  logger.info("stopped, %u B free (largest block %u B)", (unsigned)ESP.getFreeHeap(),
              (unsigned)ESP.getMaxAllocHeap());
  return true;
}

bool EcoflowClient::start() {
  if (!_started) {
    // Ще жодного begin() не було (напр. autoconnect вимкнено) - піднімаємо з нуля.
    begin();
    return _started;
  }
  if (!_mqtt.isSuspended()) {
    return true;  // вже працює
  }
  _mqtt.resume();
  logger.info("resumed, %u B free", (unsigned)ESP.getFreeHeap());
  return true;
}

bool EcoflowClient::withMqttSuspended(const char *what, const std::function<bool()> &action) {
  if (_config.accessKey == nullptr || _config.secretKey == nullptr) {
    _lastError = "REST unavailable: accessKey/secretKey not set";
    return false;
  }

  const bool needSuspend = _started && !_mqtt.isSuspended();
  if (needSuspend) {
    if (!_mqtt.suspend()) {
      // Клієнт лишився працювати - REST робити не можна: другої TLS-сесії
      // heap не витримає.
      _lastError = "failed to suspend MQTT - REST skipped";
      return false;
    }
    logger.debug("%s: MQTT suspended, %u B free (largest block %u B)", what,
                 (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());
  }

  const bool ok = action();

  if (needSuspend) {
    // Не resume() тут напряму - REST-таск ще живий (свій 16 КБ стек), а
    // resume() одразу підняв би ще один mqtt-net таск + TLS-сесію. Дочекатись
    // loop() після реального завершення таска (див. коментар у .hpp).
    _mqttResumePending = true;
  }
  return ok;
}

bool EcoflowClient::refreshDevices() {
  return withMqttSuspended("device-list", [this]() {
    if (!_auth.fetchDeviceList(_devices)) {
      _lastError = _auth.lastError();
      return false;
    }
    return true;
  });
}

bool EcoflowClient::refreshCredentials(EcoflowMqttCredentials &outCredentials) {
  return withMqttSuspended("certification", [this, &outCredentials]() {
    if (!_auth.fetchMqttCredentials(outCredentials)) {
      _lastError = _auth.lastError();
      return false;
    }
    return true;
  });
}
