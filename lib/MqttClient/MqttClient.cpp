#include "MqttClient.hpp"

#include <Arduino.h>

#include <algorithm>

#include "MqttTopicMatcher.hpp"

MqttClient* MqttClient::_instance = nullptr;

MqttClient::MqttClient(const MqttConfig& config)
    : _config(config), _defaultKeyGenerator(config.prefix) {}

void MqttClient::setKeyGenerator(MqttKeyGenerator* keyGenerator) { _keyGenerator = keyGenerator; }

const MqttKeyGenerator& MqttClient::keyGenerator() const {
  return _keyGenerator != nullptr ? *_keyGenerator : _defaultKeyGenerator;
}

std::string MqttClient::resolveTopic(const char* topic) const {
  if (_keyGenerator != nullptr) {
    return _keyGenerator->key(topic);
  }
  return topic != nullptr ? std::string(topic) : std::string();
}

void MqttClient::begin() {
  _instance = this;

  // Якщо setKeyGenerator() не викликали заздалегідь - fallback на _config.prefix.
  if (_keyGenerator == nullptr) {
    _keyGenerator = &_defaultKeyGenerator;
  }

  if (_config.useTls) {
    if (_config.caCert != nullptr) {
#if BOARD_ESP8266
      _secureClient.setInsecure();
#else
      _secureClient.setCACert(_config.caCert);
#endif
    } else {
      _secureClient.setInsecure();
    }
    _mqttClient.setClient(_secureClient);
  } else {
    _mqttClient.setClient(_plainClient);
  }

  _mqttClient.setServer(_config.host, _config.port);
  _mqttClient.setBufferSize(_config.bufferSize);
  _mqttClient.setCallback(MqttClient::staticCallback);
}

void MqttClient::loop() {
  if (!_mqttClient.connected()) {
    uint32_t now = millis();
    if (now - _lastReconnectAttempt >= _config.reconnectIntervalMs) {
      _lastReconnectAttempt = now;
      connect();
    }
    return;
  }

  _mqttClient.loop();
}

void MqttClient::disconnect(const char* customOfflineMessage) {
  const char* message =
      customOfflineMessage != nullptr ? customOfflineMessage : _config.lwtOfflineMessage;

  bool hasLwtTopic = _config.lwtTopic != nullptr && _config.lwtTopic[0] != '\0';
  bool hasMessage = message != nullptr && message[0] != '\0';

  if (_mqttClient.connected() && hasLwtTopic && hasMessage) {
    std::string lwtTopic = resolveTopic(_config.lwtTopic);
    _mqttClient.publish(lwtTopic.c_str(), message, _config.lwtRetain);
  }

  _mqttClient.disconnect();
}

bool MqttClient::connect() {
  bool hasLwtTopic = _config.lwtTopic != nullptr && _config.lwtTopic[0] != '\0';
  bool hasOfflineMessage =
      _config.lwtOfflineMessage != nullptr && _config.lwtOfflineMessage[0] != '\0';
  bool hasOnlineMessage =
      _config.lwtOnlineMessage != nullptr && _config.lwtOnlineMessage[0] != '\0';
  bool hasLwt = hasLwtTopic && hasOfflineMessage;

  // Резолвимо LWT topic ОДИН раз (через _keyGenerator, якщо injected) і тримаємо
  // std::string живим на весь виклик connect(), бо PubSubClient::connect() лише
  // зберігає переданий const char* (не копіює його).
  std::string lwtTopic = hasLwtTopic ? resolveTopic(_config.lwtTopic) : std::string();

  bool ok;
  if (hasLwt) {
    ok = _config.useAuth ? _mqttClient.connect(_config.clientId, _config.username, _config.password,
                                               lwtTopic.c_str(), _config.lwtQos, _config.lwtRetain,
                                               _config.lwtOfflineMessage)
                         : _mqttClient.connect(_config.clientId, lwtTopic.c_str(), _config.lwtQos,
                                               _config.lwtRetain, _config.lwtOfflineMessage);
  } else {
    ok = _config.useAuth ? _mqttClient.connect(_config.clientId, _config.username, _config.password)
                         : _mqttClient.connect(_config.clientId);
  }

  if (ok) {
    resubscribeAll();

    if (hasLwtTopic && hasOnlineMessage) {
      _mqttClient.publish(lwtTopic.c_str(), _config.lwtOnlineMessage, _config.lwtRetain);
    }
  }

  return ok;
}

bool MqttClient::publish(const char* topic, const char* payload, bool retained) {
  std::string fullTopic = resolveTopic(topic);
  return _mqttClient.publish(fullTopic.c_str(), payload, retained);
}

bool MqttClient::publish(const char* topic, const uint8_t* payload, unsigned int length,
                         bool retained) {
  std::string fullTopic = resolveTopic(topic);
  return _mqttClient.publish(fullTopic.c_str(), payload, length, retained);
}

bool MqttClient::subscribe(const char* topic) {
  std::string fullTopic = resolveTopic(topic);
  return _mqttClient.subscribe(fullTopic.c_str());
}

bool MqttClient::unsubscribe(const char* topic) {
  std::string fullTopic = resolveTopic(topic);
  return _mqttClient.unsubscribe(fullTopic.c_str());
}

MqttListenerId MqttClient::addListener(const char* topic, MqttListenerCallback callback) {
  MqttListenerEntry entry;
  entry.id = _nextListenerId++;
  entry.topic = resolveTopic(topic).c_str();
  entry.callback = callback;
  _listeners.push_back(entry);

  if (_mqttClient.connected()) {
    _mqttClient.subscribe(entry.topic.c_str());
  }

  return entry.id;
}

bool MqttClient::publishJson(const char* topic, JsonDocument& doc, bool retained) {
  size_t size = measureJson(doc);
  std::vector<char> buffer(size + 1);
  serializeJson(doc, buffer.data(), buffer.size());
  return publish(topic, reinterpret_cast<const uint8_t*>(buffer.data()), size, retained);
}

MqttListenerId MqttClient::addJsonListener(
    const char* topic, std::function<void(const char*, JsonDocument&)> callback) {
  return addListener(
      topic, [callback](const char* topic, const uint8_t* payload, unsigned int length) -> void {
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, payload, length);
        if (error) {
          return;
        }
        callback(topic, doc);
      });
}

MqttListenerId MqttClient::addStringListener(const char* topic,
                                             MqttStringListenerCallback callback) {
  return addListener(
      topic,
      [callback](const char* messageTopic, const uint8_t* payload, unsigned int length) -> void {
        std::vector<char> buffer(length + 1);
        memcpy(buffer.data(), payload, length);
        buffer[length] = '\0';
        callback(messageTopic, buffer.data());
      });
}

void MqttClient::removeListener(MqttListenerId id) {
  for (auto& entry : _listeners) {
    if (entry.id == id) {
      entry.markedForRemoval = true;
      return;
    }
  }
}

void MqttClient::cleanupRemovedListeners() {
  _listeners.erase(
      std::remove_if(_listeners.begin(), _listeners.end(),
                     [](const MqttListenerEntry& entry) { return entry.markedForRemoval; }),
      _listeners.end());
}

void MqttClient::resubscribeAll() {
  for (const auto& entry : _listeners) {
    if (!entry.markedForRemoval) {
      _mqttClient.subscribe(entry.topic.c_str());
    }
  }
}

void MqttClient::dispatchMessage(const char* topic, const uint8_t* payload, unsigned int length) {
  for (auto& entry : _listeners) {
    if (entry.markedForRemoval) {
      continue;
    }

    if (!entry.callback) {
      continue;
    }

    if (!MqttTopicMatcher::match(entry.topic.c_str(), topic)) {
      continue;
    }

    entry.callback(topic, payload, length);
  }

  cleanupRemovedListeners();
}

bool MqttClient::isConnected() const { return const_cast<PubSubClient&>(_mqttClient).connected(); }

void MqttClient::handleMessage(char* topic, uint8_t* payload, unsigned int length) {
  dispatchMessage(topic, payload, length);
}

void MqttClient::staticCallback(char* topic, uint8_t* payload, unsigned int length) {
  if (_instance != nullptr) {
    _instance->handleMessage(topic, payload, length);
  }
}
