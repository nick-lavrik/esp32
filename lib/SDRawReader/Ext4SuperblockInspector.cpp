#include "Ext4SuperblockInspector.hpp"

#include <cstdio>
#include <cstring>

// Зміщення полів у суперблоці ext4 (struct ext4_super_block, fs/ext4/ext4.h
// ядра Linux). Названі як у ядрі, щоб їх можна було звірити з першоджерелом.
namespace {

constexpr size_t kSInodesCount = 0x00;
constexpr size_t kSBlocksCountLo = 0x04;
constexpr size_t kSFreeBlocksCountLo = 0x0C;
constexpr size_t kSFreeInodesCount = 0x10;
constexpr size_t kSFirstDataBlock = 0x14;
constexpr size_t kSLogBlockSize = 0x18;
constexpr size_t kSBlocksPerGroup = 0x20;
constexpr size_t kSInodesPerGroup = 0x28;
constexpr size_t kSMTime = 0x2C;   // останнє монтування (unix time)
constexpr size_t kSWTime = 0x30;   // останній запис (unix time)
constexpr size_t kSMntCount = 0x34;
constexpr size_t kSMagic = 0x38;
constexpr size_t kSState = 0x3A;
constexpr size_t kSErrors = 0x3C;
constexpr size_t kSLastCheck = 0x40;
constexpr size_t kSInodeSize = 0x58;
constexpr size_t kSFeatureCompat = 0x5C;
constexpr size_t kSFeatureIncompat = 0x60;
constexpr size_t kSFeatureRoCompat = 0x64;
constexpr size_t kSUuid = 0x68;         // 16 байт
constexpr size_t kSVolumeName = 0x78;   // 16 байт, може бути без '\0'
constexpr size_t kSLastMounted = 0x88;  // 64 байти, шлях останнього монтування
constexpr size_t kSMkfsTime = 0x108;
constexpr size_t kSBlocksCountHi = 0x150;
constexpr size_t kSFreeBlocksCountHi = 0x158;

// s_state (бітова маска)
constexpr uint16_t kStateCleanlyUnmounted = 0x0001;
constexpr uint16_t kStateErrorsDetected = 0x0002;
constexpr uint16_t kStateOrphansRecovering = 0x0004;

// s_feature_incompat — лише ті, що впливають на монтування/відновлення
constexpr uint32_t kIncompatFileType = 0x0002;
constexpr uint32_t kIncompatRecover = 0x0004;  // журнал НЕ доіграний
constexpr uint32_t kIncompatJournalDev = 0x0008;
constexpr uint32_t kIncompatMetaBg = 0x0010;
constexpr uint32_t kIncompatExtents = 0x0040;
constexpr uint32_t kIncompat64Bit = 0x0080;
constexpr uint32_t kIncompatFlexBg = 0x0200;
constexpr uint32_t kIncompatEaInode = 0x0400;
constexpr uint32_t kIncompatCsumSeed = 0x2000;
constexpr uint32_t kIncompatLargeDir = 0x4000;
constexpr uint32_t kIncompatInlineData = 0x8000;
constexpr uint32_t kIncompatEncrypt = 0x10000;

constexpr uint32_t kCompatHasJournal = 0x0004;
constexpr uint32_t kRoCompatMetadataCsum = 0x0400;

// Копіює рядкове поле фіксованої довжини у C-рядок: у суперблоці такі поля
// можуть бути заповнені під саму межу БЕЗ завершального нуля, тому strncpy
// напряму в лог давав би читання за межами буфера.
void copyFixedString(const uint8_t *src, size_t maxLength, char *out, size_t outSize) {
  const size_t limit = (maxLength < outSize - 1) ? maxLength : (outSize - 1);
  size_t i = 0;
  for (; i < limit && src[i] != 0; ++i) {
    // Непечатні байти замінюємо на '?': сміття в мітці не повинно ламати лог.
    out[i] = (src[i] >= 0x20 && src[i] < 0x7F) ? static_cast<char>(src[i]) : '?';
  }
  out[i] = '\0';
}

// Розмір у байтах -> "<число> <одиниця>". Локальна реалізація, бо
// SizeFormatter лежить у src/ і бібліотекам з lib/ недоступний.
void formatSize(uint64_t bytes, char *out, size_t outSize) {
  static constexpr const char *kUnits[] = {"B", "KiB", "MiB", "GiB", "TiB"};
  double value = static_cast<double>(bytes);
  int unitIndex = 0;
  while (value >= 1024.0 && unitIndex < 4) {
    value /= 1024.0;
    ++unitIndex;
  }
  snprintf(out, outSize, "%.2f %s", value, kUnits[unitIndex]);
}

}  // namespace

uint16_t Ext4SuperblockInspector::readLE16(const uint8_t *p) {
  return static_cast<uint16_t>(p[0]) | static_cast<uint16_t>(static_cast<uint16_t>(p[1]) << 8);
}

uint32_t Ext4SuperblockInspector::readLE32(const uint8_t *p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

bool Ext4SuperblockInspector::hasMagic(const uint8_t *superblock) {
  return superblock != nullptr && readLE16(superblock + kSMagic) == kMagic;
}

void Ext4SuperblockInspector::printFeatures(const uint8_t *superblock, Print &out) {
  const uint32_t compat = readLE32(superblock + kSFeatureCompat);
  const uint32_t incompat = readLE32(superblock + kSFeatureIncompat);
  const uint32_t roCompat = readLE32(superblock + kSFeatureRoCompat);

  out.printf("features    : compat=0x%08lX incompat=0x%08lX ro_compat=0x%08lX\n",
             static_cast<unsigned long>(compat), static_cast<unsigned long>(incompat),
             static_cast<unsigned long>(roCompat));

  // Рядок прапорців збираємо в один буфер: логер додає префікс на кожен
  // println(), тому по одному прапорцю в рядок було б 12 рядків шуму.
  char flags[192];
  int pos = snprintf(flags, sizeof(flags), "flags       :");

  const struct {
    uint32_t mask;
    const char *name;
  } kIncompatNames[] = {
      {kIncompatFileType, " filetype"}, {kIncompatRecover, " needs_recovery"},
      {kIncompatJournalDev, " journal_dev"}, {kIncompatMetaBg, " meta_bg"},
      {kIncompatExtents, " extents"}, {kIncompat64Bit, " 64bit"},
      {kIncompatFlexBg, " flex_bg"}, {kIncompatEaInode, " ea_inode"},
      {kIncompatCsumSeed, " csum_seed"}, {kIncompatLargeDir, " large_dir"},
      {kIncompatInlineData, " inline_data"}, {kIncompatEncrypt, " encrypt"},
  };

  for (const auto &entry : kIncompatNames) {
    if ((incompat & entry.mask) != 0) {
      pos += snprintf(flags + pos, sizeof(flags) - pos, "%s", entry.name);
    }
  }

  if ((compat & kCompatHasJournal) != 0) {
    pos += snprintf(flags + pos, sizeof(flags) - pos, " has_journal");
  }
  if ((roCompat & kRoCompatMetadataCsum) != 0) {
    snprintf(flags + pos, sizeof(flags) - pos, " metadata_csum");
  }

  out.println(flags);

  // Це не косметика, а робоча інструкція: якщо журнал не доіграний, монтувати
  // розділ у режимі read-only можна ЛИШЕ з опцією noload, інакше ядро
  // спробує відновити журнал і піде на запис у картку, що вмирає.
  if ((incompat & kIncompatRecover) != 0) {
    out.println("WARNING: needs_recovery - the journal was NOT replayed.");
    out.println("       Mount only as: mount -o ro,noload");
  }
}

void Ext4SuperblockInspector::printAll(const uint8_t *superblock, Print &out) {
  out.println(F("=== ext2/3/4 Superblock ==="));

  if (superblock == nullptr) {
    out.println("buffer not provided (nullptr)");
    return;
  }

  if (!hasMagic(superblock)) {
    out.printf("magic       : 0x%04X (expected 0x%04X) - this is NOT ext2/3/4\n",
               static_cast<unsigned int>(readLE16(superblock + kSMagic)),
               static_cast<unsigned int>(kMagic));
    out.println("Either the partition offset is wrong, or the superblock is damaged.");
    out.println("Look for backups at the start of block groups (typically block 32768).");
    return;
  }

  // Розмір блоку: у суперблоці зберігається логарифм (0 -> 1 KiB, 2 -> 4 KiB).
  const uint32_t logBlockSize = readLE32(superblock + kSLogBlockSize);
  const uint64_t blockSize = 1024ULL << logBlockSize;

  // 64-бітні лічильники: старші половини лежать окремо (s_*_hi) і мають сенс
  // лише при увімкненому incompat-прапорці 64bit. Для 232 GiB розділу з
  // 4 KiB блоками 32 біт формально ще вистачає, але читаємо повні 64 —
  // інакше на картці >16 TiB цифри були б неправильні.
  const uint32_t incompat = readLE32(superblock + kSFeatureIncompat);
  const bool is64Bit = (incompat & kIncompat64Bit) != 0;

  uint64_t blocksCount = readLE32(superblock + kSBlocksCountLo);
  uint64_t freeBlocks = readLE32(superblock + kSFreeBlocksCountLo);
  if (is64Bit) {
    blocksCount |= static_cast<uint64_t>(readLE32(superblock + kSBlocksCountHi)) << 32;
    freeBlocks |= static_cast<uint64_t>(readLE32(superblock + kSFreeBlocksCountHi)) << 32;
  }

  const uint64_t usedBlocks = (freeBlocks <= blocksCount) ? (blocksCount - freeBlocks) : 0;

  char label[17];
  copyFixedString(superblock + kSVolumeName, 16, label, sizeof(label));

  char lastMounted[65];
  copyFixedString(superblock + kSLastMounted, 64, lastMounted, sizeof(lastMounted));

  out.printf("magic       : 0x%04X (OK)\n", static_cast<unsigned int>(kMagic));
  out.printf("label       : %s\n", (label[0] != '\0') ? label : "(empty)");
  out.printf("last mounted: %s\n", (lastMounted[0] != '\0') ? lastMounted : "(unknown)");

  const uint8_t *uuid = superblock + kSUuid;
  out.printf("UUID        : %02X%02X%02X%02X-%02X%02X-%02X%02X-%02X%02X-%02X%02X%02X%02X%02X%02X\n",
             uuid[0], uuid[1], uuid[2], uuid[3], uuid[4], uuid[5], uuid[6], uuid[7], uuid[8],
             uuid[9], uuid[10], uuid[11], uuid[12], uuid[13], uuid[14], uuid[15]);

  char totalStr[24];
  char usedStr[24];
  char freeStr[24];
  formatSize(blocksCount * blockSize, totalStr, sizeof(totalStr));
  formatSize(usedBlocks * blockSize, usedStr, sizeof(usedStr));
  formatSize(freeBlocks * blockSize, freeStr, sizeof(freeStr));

  out.printf("block size  : %lu B\n", static_cast<unsigned long>(blockSize));
  out.printf("blocks      : %llu total / %llu free\n",
             static_cast<unsigned long long>(blocksCount),
             static_cast<unsigned long long>(freeBlocks));
  out.printf("size        : %s total\n", totalStr);
  // Головна цифра для планування порятунку: копіювати треба саме used, а не
  // весь розділ (e2image -ra / partclone.ext4 читають лише зайняті блоки).
  out.printf("USED        : %s  <- this much real data\n", usedStr);
  out.printf("free        : %s\n", freeStr);

  out.printf("inodes      : %lu total / %lu free\n",
             static_cast<unsigned long>(readLE32(superblock + kSInodesCount)),
             static_cast<unsigned long>(readLE32(superblock + kSFreeInodesCount)));
  out.printf("inode size  : %u B\n", static_cast<unsigned int>(readLE16(superblock + kSInodeSize)));
  out.printf("per group   : %lu blocks / %lu inodes\n",
             static_cast<unsigned long>(readLE32(superblock + kSBlocksPerGroup)),
             static_cast<unsigned long>(readLE32(superblock + kSInodesPerGroup)));
  out.printf("first block : %lu\n",
             static_cast<unsigned long>(readLE32(superblock + kSFirstDataBlock)));

  const uint16_t state = readLE16(superblock + kSState);
  char stateStr[64];
  snprintf(stateStr, sizeof(stateStr), "0x%04X%s%s%s", static_cast<unsigned int>(state),
           (state & kStateCleanlyUnmounted) ? " clean" : " NOT-CLEAN",
           (state & kStateErrorsDetected) ? " ERRORS-DETECTED" : "",
           (state & kStateOrphansRecovering) ? " ORPHANS" : "");
  out.printf("state       : %s\n", stateStr);
  out.printf("errors mode : %u (1=continue 2=remount-ro 3=panic)\n",
             static_cast<unsigned int>(readLE16(superblock + kSErrors)));
  out.printf("mount count : %u\n", static_cast<unsigned int>(readLE16(superblock + kSMntCount)));

  // Часові поля - сирий unix time: перетворення в дату на пристрої не робимо
  // (локальний час плати може бути не синхронізований, а UTC-рядок з
  // gmtime() потягнув би ще й локаль). Хост розшифрує: date -d @<число>
  out.printf("mkfs  time  : %lu (unix)\n",
             static_cast<unsigned long>(readLE32(superblock + kSMkfsTime)));
  out.printf("mount time  : %lu (unix)\n",
             static_cast<unsigned long>(readLE32(superblock + kSMTime)));
  out.printf("write time  : %lu (unix)\n",
             static_cast<unsigned long>(readLE32(superblock + kSWTime)));
  out.printf("last check  : %lu (unix)\n",
             static_cast<unsigned long>(readLE32(superblock + kSLastCheck)));

  printFeatures(superblock, out);
}
