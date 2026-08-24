#pragma once

#include <Print.h>

#include <cstddef>
#include <cstdint>

// Розбір суперблока ext2/ext3/ext4 — файлової системи, яку ESP32 змонтувати
// НЕ вміє (у ESP-IDF є лише FatFs/LittleFS/SPIFFS).
//
// НАВІЩО: коли SD картку не бачить хост-комп'ютер, а ESP32 картку читає, це
// єдиний спосіб дізнатися стан Linux-розділу до того, як витрачати дні на
// побайтове копіювання: чи живий суперблок, скільько даних РЕАЛЬНО зайнято
// (розділ на 232 GiB може містити 15 GiB даних), чи потрібне відновлення
// журналу, під якою міткою й UUID картку шукати на хості.
//
// Суперблок лежить за фіксованим зміщенням 1024 байти від початку РОЗДІЛУ
// (не картки) і займає 1024 байти, тобто сектори (firstLBA + 2) і
// (firstLBA + 3) при 512-байтових секторах. Резервні копії суперблока
// зазвичай лежать на початку груп блоків (типово блоки 32768, 98304, ...) —
// їх адреси можна порахувати з полів s_blocks_per_group / s_first_data_block,
// які друкує printAll(), і перевірити тим самим методом.
//
// Приклад:
//   uint8_t sb[Ext4SuperblockInspector::kSuperblockSize];
//   SDRawReader::readSector(SD, firstLba + 2, sb);
//   SDRawReader::readSector(SD, firstLba + 3, sb + SDRawReader::kSectorSize);
//   Ext4SuperblockInspector::printAll(sb, logger);
class Ext4SuperblockInspector {
public:
  static constexpr uint16_t kMagic = 0xEF53;             // s_magic живої ext2/3/4
  static constexpr uint32_t kSuperblockByteOffset = 1024;  // від початку розділу
  static constexpr size_t kSuperblockSize = 1024;          // стільки треба прочитати

  // Зміщення суперблока в секторах від початку розділу (при 512 B/сектор).
  static constexpr uint32_t kSuperblockSectorOffset = kSuperblockByteOffset / 512;
  static constexpr uint32_t kSuperblockSectorCount = kSuperblockSize / 512;

  // true, якщо на зміщенні 0x38 стоїть 0xEF53. Єдина швидка перевірка "чи це
  // взагалі ext*": решту полів читати без неї немає сенсу.
  static bool hasMagic(const uint8_t *superblock);

  // Друкує розібраний суперблок у людському вигляді. Якщо магії немає —
  // друкує один рядок з фактичним значенням поля і виходить.
  static void printAll(const uint8_t *superblock, Print &out);

private:
  Ext4SuperblockInspector() = delete;

  static uint16_t readLE16(const uint8_t *p);
  static uint32_t readLE32(const uint8_t *p);

  // Розшифровка прапорців features у рядок. Друкуються лише ті прапорці, що
  // впливають на порятунок даних (сумісність з версією e2fsprogs на хості,
  // потреба recovery журналу, 64-бітна адресація).
  static void printFeatures(const uint8_t *superblock, Print &out);
};
