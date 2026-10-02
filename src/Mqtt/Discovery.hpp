#pragma once

// MQTT discovery: retained devices/<client-id>/discovery з
// {"board","revision","features","commands"} (docs/mqtt-topics.md).
//
//   mqtt.onConnect([](const MqttTransportClient&) { publishDiscovery(); });
//
// Публікується на КОЖЕН (пере)конект, не одноразово. "board" - BOARD_NAME
// з розділу 1 src-<env>/environment.h.

#include "features.h"

#if HAS_MQTT_CLIENT && !ESP8266
void publishDiscovery();
#else
// ESP8266: PubSubClient-гілка не кличе onConnect-колбек, discovery немає.
inline void publishDiscovery() {}
#endif
