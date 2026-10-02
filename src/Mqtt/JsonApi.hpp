#pragma once

// MQTT JSON API (SAPI): read-only команди devices/<client-id>/api/<cmd> ->
// .../reply (docs/mqtt-topics.md, docs/web_portal.md «MQTT JSON API (SAPI)»).
//
//   registerJsonApiCommands();  // з setupMqttClient(), до mqtt-конекту не обов'язково
//
// Підписки лише реєструються в mqtt; виконання - через CommandQueue в loop().
// Команди, що читають провайдери порталу (system-info, wifi-*, fs-*, nvs-*,
// ecoflow-*, mqtt-status), є лише з HAS_WEB_PORTAL; commands-list - завжди.

#include <stddef.h>

#include "features.h"

#if HAS_MQTT_CLIENT && !ESP8266
void registerJsonApiCommands();

// Імена зареєстрованих команд - для discovery ("commands").
size_t jsonApiCommandCount();
const char* jsonApiCommandName(size_t index);
#else
// ESP8266: порталу немає (HAS_WEB_PORTAL=0), discovery не компілюється
// (Discovery.hpp) - реєстр імен і commands-list там нікому не потрібні.
inline void registerJsonApiCommands() {}
#endif
