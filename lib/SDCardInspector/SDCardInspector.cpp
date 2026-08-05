#include "SDCardInspector.hpp"

#include <cstdio>

// Найпоширеніші коди типів розділів MBR (partition type / system ID).
// Повний перелік значно довший (сотні кодів для різних старих ОС),
// тут наведено лише ті, що реально трапляються на SD картках і флешках.
std::string SDCardInspector::partitionTypeToString(uint8_t typeCode) {
  switch (typeCode) {
    case 0x01:
      return "FAT12";
    case 0x04:
      return "FAT16 (<32MB)";
    case 0x05:
      return "Extended (CHS)";
    case 0x06:
      return "FAT16";
    case 0x07:
      return "NTFS/exFAT";
    case 0x0B:
      return "FAT32 (CHS)";
    case 0x0C:
      return "FAT32 (LBA)";
    case 0x0E:
      return "FAT16 (LBA)";
    case 0x0F:
      return "Extended (LBA)";
    case 0x82:
      return "Linux swap";
    case 0x83:
      return "Linux";
    case 0x8E:
      return "Linux LVM";
    case 0xA5:
      return "FreeBSD";
    case 0xEE:
      return "GPT protective (реальні розділи в GPT)";
    case 0xEF:
      return "EFI System";
    default: {
      char buffer[8];
      snprintf(buffer, sizeof(buffer), "0x%02X", typeCode);
      return std::string(buffer);
    }
  }
}
