#include "AnalogSensorMqttReporter.hpp"

#include <ArduinoJson.h>

AnalogSensorMqttReporter::AnalogSensorMqttReporter(AnalogSensor& sensor, MqttClient& mqttClient,
                                                   const char* topic, uint32_t intervalMs,
                                                   uint8_t deltaThresholdPercent)
    : _sensor(sensor),
      _mqttClient(mqttClient),
      _topic(topic),
      _intervalMs(intervalMs),
      _deltaThresholdPercent(deltaThresholdPercent) {}

void AnalogSensorMqttReporter::update() {
  _sensor.update();

  /* uint8_t currentPercent = _sensor.percent();
  unsigned long now = millis();

  if (_shouldPublish(currentPercent, now)) {
      _publish(currentPercent);
  } */
}

bool AnalogSensorMqttReporter::_shouldPublish(uint8_t currentPercent, unsigned long now) const {
  if (!_hasPublished) {
    return true;
  }

  bool intervalElapsed = (now - _lastPublishMs) >= _intervalMs;

  uint8_t delta = (currentPercent > _lastPublishedPercent)
                      ? (currentPercent - _lastPublishedPercent)
                      : (_lastPublishedPercent - currentPercent);
  bool thresholdExceeded = delta >= _deltaThresholdPercent;

  return intervalElapsed || thresholdExceeded;
}

void AnalogSensorMqttReporter::_publish(uint8_t percent) {
  // ArduinoJson v7 API: a single general-purpose JsonDocument (no template size).
  JsonDocument doc;
  doc["value"] = percent;
  doc["ts"] = millis();

  String payload;
  serializeJson(doc, payload);

  /* if (_mqttClient.publish(_topic.c_str(), payload)) {
      _lastPublishMs = millis();
      _lastPublishedPercent = percent;
      _hasPublished = true;
  } */
}
