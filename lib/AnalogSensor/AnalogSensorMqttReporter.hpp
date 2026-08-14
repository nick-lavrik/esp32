#pragma once

#if USE_MQTT_CLIENT
#include <Arduino.h>

#include "AnalogSensor.hpp"
#include "MqttClient.hpp"

/**
 * AnalogSensorMqttReporter
 *
 * Periodically reads an AnalogSensor (as a 0-100 percent value) and
 * publishes it to MQTT as JSON:
 *   {"value":<uint8_t 0-100>,"ts":<millis>}
 *
 * Publishes whenever either condition is met (whichever comes first):
 *   - the fixed interval elapses (heartbeat, so a consumer sees it's alive), or
 *   - the percent reading changes by more than `deltaThresholdPercent` since
 *     the last publish.
 *
 * Call `update()` frequently (e.g. from a CronTask running every ~200-500ms).
 * Both dependencies are injected by reference (constructor injection),
 * matching the project's existing DI convention — this class does not own
 * the AnalogSensor or MqttClient instances. Fully generic: works for any
 * analog sensor (light, moisture, gas, etc.), the MQTT topic is what gives
 * it meaning.
 *
 * NOTE: `ts` is `millis()` (device uptime), not wall-clock time. If you need
 * an epoch timestamp, sync time via NTP elsewhere and pass a time source in;
 * this class deliberately stays free of NTP/RTC dependencies.
 */
class AnalogSensorMqttReporter {
public:
  AnalogSensorMqttReporter(AnalogSensor& sensor, MqttClient& mqttClient, const char* topic,
                           uint32_t intervalMs = 5000, uint8_t deltaThresholdPercent = 5);

  // Reads the sensor and publishes if due. Safe to call often.
  void update();

  void setIntervalMs(uint32_t intervalMs) { _intervalMs = intervalMs; }
  void setDeltaThresholdPercent(uint8_t deltaThresholdPercent) {
    _deltaThresholdPercent = deltaThresholdPercent;
  }

private:
  bool _shouldPublish(uint8_t currentPercent, unsigned long now) const;
  void _publish(uint8_t percent);

  AnalogSensor& _sensor;
  MqttClient& _mqttClient;
  String _topic;

  uint32_t _intervalMs;
  uint8_t _deltaThresholdPercent;

  unsigned long _lastPublishMs = 0;
  uint8_t _lastPublishedPercent = 0;
  bool _hasPublished = false;
};
#endif