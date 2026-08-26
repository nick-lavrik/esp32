#include "EcoFlowClient.hpp"

#include <TLogger.hpp>

#include "EcoFlowDeviceRegistry.hpp"
#include "EcoFlowMqttTopics.hpp"

namespace {
TLogger logger{"ecoflow"};
}

std::string EcoFlowClient::buildRootTopic(const char *account) {
  // Один root-топік на весь акаунт: далі MqttClient сам роздає повідомлення по
  // фільтрах addListener(). Глобальне "#" тут заборонене ACL-ом брокера.
  if (account == nullptr || account[0] == '\0') {
    return std::string();
  }
  return std::string("/open/") + account + "/#";
}

MqttConfig EcoFlowClient::makeMqttConfig(const Config &config, const std::string &rootTopic) {
  MqttConfig mqttConfig;
  mqttConfig.host = config.mqttHost;
  mqttConfig.port = config.mqttPort;
  mqttConfig.clientId = config.clientId;

  mqttConfig.useAuth = true;
  mqttConfig.username = config.mqttUsername;
  mqttConfig.password = config.mqttPassword;

  // Брокер EcoFlow приймає лише mqtts. caCert не задаємо -> setInsecure();
  // TODO(production): закріпити CA-сертифікат, як і в EcoFlowAuthClient.
  mqttConfig.useTls = true;
  mqttConfig.caCert = nullptr;

  // Топіки EcoFlow передаються брокеру байт-у-байт разом із провідним '/'
  // (MqttKeyGenerator його зрізав би - див. MqttConfig::useKeyGenerator).
  mqttConfig.useKeyGenerator = false;
  mqttConfig.rootSubscribeTopic = rootTopic.c_str();

  // quota великих станцій (Delta Pro тощо) перевищує дефолтні 2 КБ.
  mqttConfig.rootSubscribeBufferSize = 8 * 1024;

  // mbedTLS-хендшейк не вміщується у дефолтні 8 КБ стеку мережевого таска.
  mqttConfig.taskStackSize = 16 * 1024;
  mqttConfig.taskName = "ecoflow-net";

  // LWT не задаємо: брокер EcoFlow не дозволяє публікацію в довільні топіки.

  return mqttConfig;
}

EcoFlowClient::EcoFlowClient(const Config &config)
    : _config(config),
      _account(config.mqttUsername != nullptr ? config.mqttUsername : ""),
      _rootTopicStorage(buildRootTopic(config.mqttUsername)),
      _mqtt(makeMqttConfig(config, _rootTopicStorage)),
      _auth(config.accessKey != nullptr ? config.accessKey : "",
            config.secretKey != nullptr ? config.secretKey : "") {}

String EcoFlowClient::serialFromTopic(const char *topic) {
  // "/open/{account}/{sn}/quota" -> {sn} - це третій сегмент (перший порожній
  // через провідний '/').
  if (topic == nullptr) {
    return String();
  }

  String value(topic);
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

void EcoFlowClient::begin() {
  if (_started) {
    return;
  }

  if (_account.length() == 0) {
    _lastError = "mqttUsername (certificateAccount) not set";
    logger.error("%s", _lastError.c_str());
    return;
  }

  // Серійні номери беремо з ПРОШИТОГО переліку (EcoFlowDeviceRegistry), а не з
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
  const EcoFlowDeviceInfo *table = EcoFlowDeviceRegistry::deviceTable();
  for (size_t i = 0; i < EcoFlowDeviceRegistry::deviceCount(); i++) {
    const String quotaTopic = EcoFlowMqttTopics::quota(_account, table[i].serialNumber);
    const String statusTopic = EcoFlowMqttTopics::status(_account, table[i].serialNumber);
    logger.debug("%-16s %s", table[i].serialNumber, table[i].name);

    _mqtt.addJsonListener(quotaTopic.c_str(), [this](const char *topic, JsonDocument &doc) {
      _messageCount++;
      _lastTopic = topic;
      if (_quotaCallback) {
        _quotaCallback(serialFromTopic(topic), doc);
      }
    });

    _mqtt.addJsonListener(statusTopic.c_str(), [this](const char *topic, JsonDocument &doc) {
      _messageCount++;
      _lastTopic = topic;
      if (_statusCallback) {
        _statusCallback(serialFromTopic(topic), doc);
      }
    });
  }

  _mqtt.begin();
  _started = true;
  logger.info("%s:%u account=%s, subscriptions: %u", _config.mqttHost, (unsigned)_config.mqttPort,
              _account.c_str(), (unsigned)(EcoFlowDeviceRegistry::deviceCount() * 2));
}

void EcoFlowClient::loop() { _mqtt.loop(); }

// Стек REST-таска: TLS-хендшейк (mbedTLS RSA/ECDHE) + HTTPClient + розбір JSON.
// 16 КБ - той самий порядок, що й у мережевого таска MQTT з TLS.
static constexpr uint32_t kRestTaskStackSize = 16 * 1024;

struct EcoFlowRestTaskArg {
  EcoFlowClient *self;
  int job;
};

void EcoFlowClient::restTaskTrampoline(void *param) {
  auto *arg = static_cast<EcoFlowRestTaskArg *>(param);
  EcoFlowClient *self = arg->self;
  const RestJob job = static_cast<RestJob>(arg->job);
  delete arg;

  self->runRestJob(job);

  // Скільки стеку лишилось невикористаним - якщо тут близько до нуля,
  // kRestTaskStackSize треба піднімати.
  logger.debug("rest task finished, stack headroom %u B",
               (unsigned)(uxTaskGetStackHighWaterMark(nullptr) * sizeof(StackType_t)));

  self->_restBusy = false;
  vTaskDelete(nullptr);
}

void EcoFlowClient::runRestJob(RestJob job) {
  if (job == RestJob::kStart) {
    begin();
    return;
  }

  if (job == RestJob::kDevices) {
    if (!refreshDevices()) {
      logger.error("device list fail: %s", _lastError.c_str());
      return;
    }
    logger.info("fetched %u devices:", (unsigned)_devices.size());
    for (const auto &device : _devices) {
      logger.info("  %s %-20s %s", device.serialNumber.c_str(), device.name.c_str(),
                  device.online ? "online" : "offline");
    }
    logger.info("new devices are picked up at startup only - reboot required");
    return;
  }

  EcoFlowMqttCredentials credentials;
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

bool EcoFlowClient::startRestTask(RestJob job) {
  if (_restBusy) {
    _lastError = "a REST request is already running";
    return false;
  }
  _restBusy = true;

  auto *arg = new EcoFlowRestTaskArg{this, static_cast<int>(job)};
  // xTaskCreate без пінінгу - ESP32-C6 single-core (див. MqttClient::begin()).
  if (xTaskCreate(&EcoFlowClient::restTaskTrampoline, "ecoflow-rest", kRestTaskStackSize, arg,
                  tskIDLE_PRIORITY + 1, nullptr) != pdPASS) {
    delete arg;
    _restBusy = false;
    _lastError = "failed to create rest task (out of memory?)";
    return false;
  }
  return true;
}

bool EcoFlowClient::beginAsync() { return startRestTask(RestJob::kStart); }

bool EcoFlowClient::refreshDevicesAsync() { return startRestTask(RestJob::kDevices); }

bool EcoFlowClient::refreshCredentialsAsync() { return startRestTask(RestJob::kCredentials); }

bool EcoFlowClient::withMqttSuspended(const char *what, const std::function<bool()> &action) {
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
    _mqtt.resume();
  }
  return ok;
}

bool EcoFlowClient::refreshDevices() {
  return withMqttSuspended("device-list", [this]() {
    if (!_auth.fetchDeviceList(_devices)) {
      _lastError = _auth.lastError();
      return false;
    }
    return true;
  });
}

bool EcoFlowClient::refreshCredentials(EcoFlowMqttCredentials &outCredentials) {
  return withMqttSuspended("certification", [this, &outCredentials]() {
    if (!_auth.fetchMqttCredentials(outCredentials)) {
      _lastError = _auth.lastError();
      return false;
    }
    return true;
  });
}
