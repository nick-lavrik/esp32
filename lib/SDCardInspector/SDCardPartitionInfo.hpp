#pragma once

#include <cstdint>
#include <string>

// Один запис у MBR (Master Boot Record) таблиці розділів SD картки.
// Заповнюється класом SDCardInspector.
struct SDCardPartitionInfo {
  uint8_t index = 0;      // порядковий номер розділу в MBR: 1..4
  bool bootable = false;  // прапорець active/boot (байт 0x80 у записі)
  uint8_t typeCode = 0;  // сирий байт типу розділу (напр. 0x0C = FAT32 LBA)
  std::string typeName;  // людський опис типу ("FAT32 (LBA)", "exFAT/NTFS" тощо)
  uint32_t firstSectorLBA = 0;  // LBA-адреса першого сектора розділу
  uint32_t sectorCount = 0;     // кількість секторів у розділі
};
