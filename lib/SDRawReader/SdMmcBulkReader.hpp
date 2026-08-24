#pragma once

#if defined(ESP32)

// Плати без SDMMC-периферії (напр. ESP32-C6) цього класу не мають зовсім:
// там і <SD_MMC.h> порожній. SOC_SDMMC_HOST_SUPPORTED - ознака від ESP-IDF,
// а не від конкретної плати, тому guard не треба правити під нові плати.
#include <soc/soc_caps.h>
#if defined(SOC_SDMMC_HOST_SUPPORTED) && SOC_SDMMC_HOST_SUPPORTED

#include "SdBulkReader.hpp"

// Raw-читання картки в SDMMC-режимі (4-bit шина: D0..D3/CLK/CMD).
//
// НАВІЩО ОКРЕМИЙ КЛАС: у SPI-гілці вузьким місцем була одна SD-команда на
// кожні 512 байт, і його обійшли через внутрішній ff_sd_read() (CMD18). У
// SDMMC-режимі цієї проблеми немає в тій самій мірі: 4-bit шина віддає
// сектор за чотири лінії замість однієї, тому навіть посекторне читання
// через публічний SD_MMC.readRAW() швидше за пачки по SPI. Тому тут
// свідомо НЕ використовується жоден внутрішній символ фреймворку -
// клас тримається лише публічного API, і його не зламає оновлення core.
//
// Проте посекторне читання все одно платить за кожен сектор окремою
// командою, і на USB-каналі це видно: 539 KiB/s. Тому begin() пробує задіяти
// багатосекторний шлях FatFs (ff_disk_read), а якщо не вдається -
// readSectors() лишається звичайним циклом. Уся діагностика (перевірка
// повторюваності, карта деградації, голосування) працює однаково в обох
// випадках, бо живе в SdBulkReader.
class SdMmcBulkReader : public SdBulkReader {
public:
  // expectedSectors - кількість секторів УСІЄЇ картки.
  //
  // УВАГА: не передавайте сюди SD_MMC.numSectors() - він повертає розмір
  // змонтованої файлової системи (першого розділу), а не картки, і тоді
  // читання за межами того розділу відкидатиметься як вихід за межі.
  // Правильне джерело - SD_MMC.cardSize() / 512.
  bool begin(size_t expectedSectors);

  bool isReady() const override { return _isReady; }
  bool readSectors(uint32_t lba, uint32_t count, uint8_t *out) override;

  // true, якщо вдалося задіяти багатосекторне читання (див. .cpp).
  bool canReadBulk() const { return _canBulk; }

private:
  bool _isReady = false;
  bool _canBulk = false;
  uint32_t _totalSectors = 0;
};

#endif  // SOC_SDMMC_HOST_SUPPORTED
#endif  // ESP32
