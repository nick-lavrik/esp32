#pragma once

// Загальний MQTT-клієнт застосунку (`mqtt`, src/App/AppGlobals.cpp): колбеки
// конекту (discovery), runtime-префікс топіків, heartbeat у LWT-топік,
// command/<client-id> -> CommandQueue, LWT інших плат, light-sensor, SAPI
// (registerJsonApiCommands()) і команди dump-mqtt/publish/mqtt-prefix/
// console-mqtt. Топіки - docs/mqtt-topics.md.
//
//   setupMqttClient();  // з setup(), після setupConfigStorage()/setupNtpService()

#include <ResponseTarget.hpp>
#include <memory>

#include "features.h"

// Без HAS_MQTT_CLIENT лише логує "MQTT client disabled!".
void setupMqttClient();

#if HAS_MQTT_CLIENT
// Приймач відповідей на command/<client-id> -> .../reply (один на пристрій).
std::shared_ptr<ResponseTarget> mqttReplyTarget();
#endif
