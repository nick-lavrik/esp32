#pragma once

#include <Print.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "SDCardPartitionInfo.hpp"

// Читає реальну MBR-таблицю розділів (сектор 0) SD картки — на відміну від
// EspPartitionInspector, який читає внутрішню flash-таблицю ESP32.
//
// Працює як з fs::SDFS (SD.h, SPI-режим), так і з fs::SDMMCFS (SD_MMC.h,
// SDMMC-режим) — обидва класи в arduino-esp32 надають однаковий набір
// методів (readRAW/sectorSize/numSectors), але не мають спільного базового
// інтерфейсу з цими методами, тому доступ реалізовано через шаблон:
//
//   SDCardInspector::printAll(SD, Serial);       // SPI-режим
//   SDCardInspector::printAll(SD_MMC, Serial);   // SDMMC-режим
//
// Увага: більшість SD карток "з коробки" відформатовані БЕЗ MBR — FAT32/
// exFAT записана напряму в сектор 0 ("superfloppy"-формат). Це не помилка:
// collectPartitions() поверне порожній вектор, а printAll() повідомить про
// відсутність таблиці розділів окремим рядком.
//
// Обмеження: парситься лише класична MBR-таблиця з 4 первинних розділів.
// Розширені (extended/logical) розділи та GPT не розбираються. Якщо
// typeCode == 0xEE ("GPT protective"), це означає, що справжня таблиця
// розділів знаходиться в GPT-заголовку, а не в MBR.
class SDCardInspector {
public:
  // Зчитує сектор 0 SD картки та повертає до 4 первинних MBR-розділів.
  // Повертає порожній вектор, якщо сектор не вдалось прочитати або
  // сигнатура MBR (0x55 0xAA на зміщенні 510) відсутня.
  template <typename SDCardT>
  static std::vector<SDCardPartitionInfo> collectPartitions(SDCardT &sdInstance) {
    std::vector<SDCardPartitionInfo> result;

    uint8_t sector0[512] = {};
    if (!sdInstance.readRAW(sector0, 0)) {
      return result;
    }

    if (!hasValidMbrSignature(sector0)) {
      return result;
    }

    for (uint8_t i = 0; i < 4; ++i) {
      const uint8_t *entry = sector0 + kPartitionTableOffset + i * kPartitionEntrySize;
      uint8_t typeCode = entry[4];

      if (typeCode == 0x00) {
        continue;  // порожній запис — розділ не використовується
      }

      SDCardPartitionInfo info;
      info.index = static_cast<uint8_t>(i + 1);
      info.bootable = (entry[0] == 0x80);
      info.typeCode = typeCode;
      info.typeName = partitionTypeToString(typeCode);
      info.firstSectorLBA = readLE32(entry + 8);
      info.sectorCount = readLE32(entry + 12);

      result.push_back(info);
    }

    return result;
  }

  // Друкує MBR-таблицю розділів у зручному для serial-виводу вигляді.
  template <typename SDCardT>
  static void printAll(SDCardT &sdInstance, Print &out) {
    auto partitions = collectPartitions(sdInstance);

    out.println(F("=== SD Card MBR Partition Table ==="));

    if (partitions.empty()) {
      out.println(F("MBR не знайдено (картка, ймовірно, відформатована без таблиці розділів)."));
      return;
    }

    out.printf("%-4s %-5s %-6s %-20s %-12s %s\n", "idx", "boot", "type", "type name", "first LBA",
               "sectors");

    for (const auto &info : partitions) {
      out.printf("%-4u %-5s 0x%02X   %-20s %-12u %u\n", static_cast<unsigned int>(info.index),
                 info.bootable ? "yes" : "no", info.typeCode, info.typeName.c_str(),
                 static_cast<unsigned int>(info.firstSectorLBA),
                 static_cast<unsigned int>(info.sectorCount));
    }
  }

private:
  static constexpr size_t kPartitionTableOffset = 446;  // зміщення таблиці розділів у MBR
  static constexpr size_t kPartitionEntrySize = 16;  // розмір одного запису в байтах

  static bool hasValidMbrSignature(const uint8_t *sector0) {
    return sector0[510] == 0x55 && sector0[511] == 0xAA;
  }

  static uint32_t readLE32(const uint8_t *p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
  }

  // Не шаблонний метод — реалізація у .cpp.
  static std::string partitionTypeToString(uint8_t typeCode);
};
