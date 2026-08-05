#pragma once

#include <FS.h>

#include "IStaticSource.hpp"

// Віддає файли з SD-картки. Працює як з SD (SPI-режим, fs::SDFS),
// так і з SD_MMC (fs::SDMMCFS) — обидва класи в arduino-esp32 успадковують
// fs::FS, тому окремого шаблону (як у SDCardInspector) тут не потрібно.
//
// Використовувати лише на платах з BOARD_HAS_SD=1 (esp32-4848s040,
// esp32-st7789) — сама ж плата вирішує, чи інстанціювати це джерело,
// клас про це не знає.
//
// Приклад:
//   SdStaticSource source(SD);
//   compositeSource.addSource(&source);
class SdStaticSource : public IStaticSource {
public:
  explicit SdStaticSource(fs::FS& fs) : _fs(fs) {}

  bool exists(const String& path) const override;
  void handleRequest(AsyncWebServerRequest* request, const String& path) override;

private:
  fs::FS& _fs;
};
