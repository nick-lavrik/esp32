#include "WebSystemModule.hpp"

#include <EspPartitionInspector.hpp>

#include "WebJson.hpp"
#include "WebPortal.hpp"

#if defined(ESP32)
#include <esp_heap_caps.h>
#include <nvs.h>
#endif

namespace {

// Той самий набір показників, що серійна команда 'heap' (src/main.cpp):
// фрагментація важливіша за сам обсяг вільного heap - алокація падає, коли
// немає ОДНОГО суцільного блоку потрібного розміру, а не коли вільного мало
// сумарно.
String heapStatsJson() {
#if defined(ESP32)
  const size_t total = heap_caps_get_total_size(MALLOC_CAP_8BIT);
  const size_t freeNow = heap_caps_get_free_size(MALLOC_CAP_8BIT);
  const size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
  const size_t minEver = heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT);
  const unsigned fragPercent = freeNow > 0 ? (unsigned)(100 - (largest * 100) / freeNow) : 0;

  String json = "{\"totalBytes\":";
  json += (uint32_t)total;
  json += ",\"freeBytes\":";
  json += (uint32_t)freeNow;
  json += ",\"largestFreeBlockBytes\":";
  json += (uint32_t)largest;
  json += ",\"minFreeEverBytes\":";
  json += (uint32_t)minEver;
  json += ",\"fragmentationPercent\":";
  json += fragPercent;
  json += "}";
  return json;
#else
  // ESP8266: інший SDK API, без загального розміру купи й історичного
  // мінімуму - та сама гілка, що й у серійній команді 'heap'.
  const size_t freeNow = ESP.getFreeHeap();
  const size_t largest = ESP.getMaxFreeBlockSize();
  String json = "{\"totalBytes\":0,\"freeBytes\":";
  json += (uint32_t)freeNow;
  json += ",\"largestFreeBlockBytes\":";
  json += (uint32_t)largest;
  json += ",\"minFreeEverBytes\":0,\"fragmentationPercent\":";
  json += (unsigned)ESP.getHeapFragmentation();
  json += "}";
  return json;
#endif
}

String flashStatsJson() {
  String json = "{\"sizeBytes\":";
  json += (uint32_t)ESP.getFlashChipSize();
  json += ",\"speedHz\":";
  json += (uint32_t)ESP.getFlashChipSpeed();
  json += "}";
  return json;
}

// ESP8266 не має NVS у принципі - ConfigStorage там лишається шимом над
// LittleFS (lib/ConfigStorage/ConfigStorage.hpp), тому "недоступно", а не
// нулі, що виглядали б як порожнє сховище.
String nvsStatsJson() {
#if defined(ESP32)
  nvs_stats_t stats{};
  if (nvs_get_stats(nullptr, &stats) != ESP_OK) return "{\"available\":false}";

  String json = "{\"available\":true,\"usedEntries\":";
  json += (uint32_t)stats.used_entries;
  json += ",\"freeEntries\":";
  json += (uint32_t)stats.free_entries;
  json += ",\"totalEntries\":";
  json += (uint32_t)stats.total_entries;
  json += ",\"namespaceCount\":";
  json += (uint32_t)stats.namespace_count;
  json += "}";
  return json;
#else
  return "{\"available\":false}";
#endif
}

String partitionsJson() {
  String json = "[";
  const auto partitions = EspPartitionInspector::collectAll(/*computeSha256=*/false);
  for (size_t i = 0; i < partitions.size(); ++i) {
    const auto& p = partitions[i];
    if (i > 0) json += ',';
    json += "{\"label\":";
    json += webjson::quote(p.label);
    json += ",\"type\":";
    json += webjson::quote(p.typeName);
    json += ",\"subtype\":";
    json += webjson::quote(p.subtypeName);
    json += ",\"offset\":";
    json += (uint32_t)p.offset;
    json += ",\"size\":";
    json += (uint32_t)p.size;
    json += ",\"encrypted\":";
    json += webjson::boolean(p.encrypted);
    json += "}";
  }
  json += "]";
  return json;
}

}  // namespace

void WebSystemModule::registerRoutes(AsyncWebServer& server, WebPortal& portal) {
  server.on("/api/system/info", HTTP_GET, [this, &portal](AsyncWebServerRequest* request) {
    const uint32_t jobId = portal.jobs().submit([this]() { return _infoJob(); });
    if (jobId == 0) {
      request->send(503, "application/json", webjson::error("Job queue is full, try again"));
      return;
    }
    request->send(202, "application/json", String("{\"jobId\":") + jobId + "}");
  });
}

String WebSystemModule::_infoJob() {
  String json = "{\"ok\":true,\"heap\":";
  json += heapStatsJson();
  json += ",\"flash\":";
  json += flashStatsJson();
  json += ",\"nvs\":";
  json += nvsStatsJson();

  json += ",\"littlefs\":";
  size_t used = 0, total = 0;
  if (_littleFsUsage && _littleFsUsage(used, total)) {
    json += "{\"available\":true,\"usedBytes\":";
    json += (uint32_t)used;
    json += ",\"totalBytes\":";
    json += (uint32_t)total;
    json += "}";
  } else {
    json += "{\"available\":false}";
  }

  json += ",\"sd\":";
  WebSystemSdInfo sd;
  if (_sdInfo && _sdInfo(sd)) {
    json += "{\"available\":true,\"present\":";
    json += webjson::boolean(sd.present);
    json += ",\"type\":";
    json += webjson::quote(sd.cardType);
    // 64-біт: SD-картки регулярно за межами uint32_t (>4 ГБ).
    json += ",\"sizeBytes\":";
    json += (unsigned long long)sd.sizeBytes;
    json += ",\"usedBytes\":";
    json += (unsigned long long)sd.usedBytes;
    json += "}";
  } else {
    json += "{\"available\":false}";
  }

  json += ",\"partitions\":";
  json += partitionsJson();
  json += "}";
  return json;
}
