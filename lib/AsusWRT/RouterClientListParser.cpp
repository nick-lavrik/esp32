#include "RouterClientListParser.hpp"

#include <ArduinoJson.h>

bool RouterClientListParser::hasEnoughFreeHeap() {
#if defined(ESP8266) || defined(ESP32)
  return ESP.getFreeHeap() >= kMinFreeHeapBytes;
#else
  return true;
#endif
}

bool RouterClientListParser::parse(const String& rawJson, std::vector<RouterClientInfo>& outClients) {
  outClients.clear();

  // Стрімінговий (SAX-подібний через filter) парсинг ArduinoJson v7 не потрібен тут —
  // документ від get_clientlist невеликий (список клієнтів мережі), тому deserializeJson
  // у JsonDocument достатньо. JsonDocument v7 сам росте динамічно (без фіксованого розміру).
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, rawJson);
  if (err) return false;

  JsonObject clientList = doc["get_clientlist"].as<JsonObject>();
  if (clientList.isNull()) return false;

  for (JsonPair kv : clientList) {
    const char* key = kv.key().c_str();

    // del(.ClientAPILevel), del(.maclist) — це не об'єкти клієнтів, пропускаємо.
    if (strcmp(key, "ClientAPILevel") == 0 || strcmp(key, "maclist") == 0) continue;

    JsonObject entry = kv.value().as<JsonObject>();
    if (entry.isNull()) continue;

    // select(.online == "1")
    const char* isOnline = entry["isOnline"] | "";
    if (strcmp(isOnline, "1") != 0) continue;

    if (!hasEnoughFreeHeap()) {
      // Недостатньо пам'яті для ще одного елемента — повертаємо те, що встигли
      // розпарсити, а не падаємо. Викликаюча сторона сама вирішує, чи цього достатньо.
      return false;
    }

    RouterClientInfo client;
    client.name = entry["name"] | "";
    client.ip = entry["ip"] | "";
    client.mac = entry["mac"] | "";
    client.type = entry["type"] | "";
    client.vendor = entry["vendor"] | "";
    client.timer = entry["wlConnectTime"] | "";

    outClients.push_back(client);
  }

  return true;
}
