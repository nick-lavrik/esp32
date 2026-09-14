#include "ProgmemStaticSource.hpp"

#include <ESPAsyncWebServer.h>

const ProgmemAsset* ProgmemStaticSource::_find(const String& path) const {
  for (size_t i = 0; i < _count; ++i) {
    if (path == _assets[i].path) return &_assets[i];
  }
  return nullptr;
}

bool ProgmemStaticSource::exists(const String& path) const { return _find(path) != nullptr; }

void ProgmemStaticSource::handleRequest(AsyncWebServerRequest* request, const String& path) {
  const ProgmemAsset* asset = _find(path);
  if (asset == nullptr) {
    request->send(404);
    return;
  }

  // Саме ця перевантаження (з довжиною) не копіює масив у RAM - віддає його
  // порціями прямо з .rodata. beginResponse_P у 3.x - лише deprecated-обгортка
  // над нею, тому кличемо напряму.
  AsyncWebServerResponse* response =
      request->beginResponse(200, asset->contentType, asset->data, asset->length);
  if (asset->gzipped) {
    response->addHeader("Content-Encoding", "gzip");
  }
  request->send(response);
}
