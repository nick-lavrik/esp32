#include "Discovery.hpp"

#if HAS_MQTT_CLIENT && !ESP8266

#include <Arduino.h>

#include "App/AppGlobals.hpp"
#include "JsonApi.hpp"

#ifndef BOARD_NAME
#error "BOARD_NAME is not defined - add it to section 1 of this env's environment.h"
#endif

// Discovery, фаза 2 (docs/mqtt-web-handoff.md, розділ "Фаза 2"):
// board+revision+features+commands.
//
// Не під HAS_WEB_PORTAL (на відміну від SAPI JSON API вище): board і
// revision - build-time константи, не дані з WebSystemModule/WebWifiModule,
// тож жодної залежності від порталу тут немає - і не повинно бути, мета
// MQTT-каналу саме прибрати цю залежність, не додати нову.
//
// !ESP8266 тут - не архітектурне рішення, а той самий гейт, що вже стоїть
// навколо mqtt.onConnect()/onDisconnect()/onConnectionFail() у setupMqttClient():
// PubSubClient-гілка MqttClient::connect() не викликає _connected_callback
// узагалі (MqttClient.cpp:206-234, лише PicoMQTT-гілка це робить,
// MqttClient.cpp:282-294) - publishDiscovery() фізично нема звідки
// покликати на ESP8266 сьогодні.
//
// board - BOARD_NAME з розділу 1 environment.h (поруч з BOARD_XXX), НЕ
// platformio.ini's board= (грубший, ділиться між різними env -
// docs/mqtt-web-handoff.md, "Discovery payload").

namespace {
// features - пряме дзеркало активних BOARD_HAS_*/HAS_* з include/features.h
// (каталог, не окрема таблиця перейменувань - той самий принцип, що вже
// застосований до BOARD_NAME).
static String discoveryFeaturesJson() {
  String out = "[";
  bool first = true;
  for (size_t i = 0; i < features::kCount; ++i) {
    if (!features::kAll[i].active) continue;
    if (!first) out += ",";
    out += "\"";
    out += features::kAll[i].name;
    out += "\"";
    first = false;
  }
  out += "]";
  return out;
}

// commands - імена зареєстрованих JSON API команд (src/Mqtt/JsonApi.cpp,
// заповнюються при registerJsonApiCommands()). На платі без порталу масив несе
// лише ті команди, що не залежать від HAS_WEB_PORTAL (сьогодні -
// "commands-list", розділ "MQTT SAPI-канал ... спільна інфраструктура"), а
// не порожній список - "[]" був би лише якби взагалі жодної команди не
// зареєстровано (гіпотетично, HAS_MQTT_CLIENT=0 тут уже недосяжний код).
static String discoveryCommandsJson() {
  String out = "[";
  for (size_t i = 0; i < jsonApiCommandCount(); ++i) {
    if (i != 0) out += ",";
    out += "\"";
    out += jsonApiCommandName(i);
    out += "\"";
  }
  out += "]";
  return out;
}

}  // namespace

// Retained, republish на КОЖЕН (пере)конект (onConnect() у setupMqttClient),
// не одноразово при старті - інакше втрата єдиного publish лишає retained-
// слот порожнім/застарілим без жодного видимого симптома
// (docs/mqtt-web-handoff.md, розділ "\"Lossy\" для discovery").
void publishDiscovery() {
  const String topic = String("devices/") + MQTT_CLIENT_ID + "/discovery";
  String payload = "{\"board\":\"";
  payload += BOARD_NAME;
  payload += "\",\"revision\":\"";
  payload += GIT_REVISION;
  payload += "\",\"features\":";
  payload += discoveryFeaturesJson();
  payload += ",\"commands\":";
  payload += discoveryCommandsJson();
  payload += "}";
  mqtt.publish(topic.c_str(), payload.c_str(), /*retained=*/true);
}

#endif
