#pragma once

// Статика, вшита у прошивку. Потрібна як ОСТАННЯ ланка CompositeStaticSource:
// LittleFS може бути порожнім (чиста плата, ще не робили 'pio run -t uploadfs')
// або щойно стертим тим самим uploadfs - і тоді вебка віддавала б самі 404,
// тобто пристрій, який не бачить мережі, не можна було б налаштувати навіть з
// його ж точки доступу. Вшитий index.html робить портал самодостатнім.
//
// Свідомо тримає лише ПОСИЛАННЯ на масиви: вміст лежить у .rodata (PROGMEM),
// у RAM не копіюється. Таблиця записів - теж зовнішня (задає власник), бо
// вона знає, скільки файлів вшито.
//
// Приклад:
//   static const ProgmemAsset kAssets[] = {
//       {"/index.html", kIndexHtml, sizeof(kIndexHtml) - 1, "text/html", false},
//   };
//   ProgmemStaticSource fallback(kAssets, sizeof(kAssets) / sizeof(kAssets[0]));
//   composite.addSource(&fallback, /*priority=*/-100);  // нижче за LittleFS

#include <Arduino.h>

#include <cstddef>

#include "IStaticSource.hpp"

struct ProgmemAsset {
  const char* path;         // шлях запиту, з провідним слешем ("/index.html")
  const uint8_t* data;      // вміст у PROGMEM
  size_t length;            // довжина без завершального '\0'
  const char* contentType;  // "text/html", "application/javascript", ...

  // Вміст уже стиснутий gzip - додається заголовок Content-Encoding: gzip.
  // Браузер розпаковує сам; ESP нічого не розпаковує.
  bool gzipped = false;
};

class ProgmemStaticSource : public IStaticSource {
public:
  ProgmemStaticSource(const ProgmemAsset* assets, size_t count)
      : _assets(assets), _count(count) {}

  bool exists(const String& path) const override;
  void handleRequest(AsyncWebServerRequest* request, const String& path) override;

private:
  const ProgmemAsset* _find(const String& path) const;

  const ProgmemAsset* _assets;
  size_t _count;
};
