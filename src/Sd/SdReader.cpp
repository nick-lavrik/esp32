// Домен SD_READER (під HAS_SD_WORKBENCH): сирі сектори картки через
// SDRawReader - sdraw/sdext4/sdbench/sdcrc/sdverify/sdmap. Якщо колись
// знадобиться окремий прапорець, межа - цей файл.

#include "Sd.hpp"

#if HAS_SD_WORKBENCH

#include <Arduino.h>

#include <Ext4SuperblockInspector.hpp>
#include <Logger.hpp>
#include <SDRawReader.hpp>
#include <TLogger.hpp>

#include "App/AppGlobals.hpp"
#include "Screen/DisplayBusYield.hpp"
#include "SdBus.hpp"

// ---------------------------------------------------------------------------
// Порятунок даних з картки, яку не бачить хост (команди "sdraw" / "sdext4").
//
// НАВІЩО: коли SD картку не визначає комп'ютер, а ESP32 її читає, плата стає
// єдиним каналом доступу до даних. Обидві команди працюють в обхід файлової
// системи, тому не залежать від того, чи вміє ESP32 монтувати те, що на
// картці — ext4 (тип розділу 0x83) він не вміє в принципі.
//
// Спільна для обох перевірка cardType() != CARD_NONE — не косметика:
// SDFS::readRAW() на незмонтованій картці передає _pdrv == 0xFF прямо в
// ff_sd_read(), який індексує s_cards[pdrv] без перевірки меж, і плата
// ресетиться (та сама пастка, що описана вище для "status sd").
// ---------------------------------------------------------------------------

// Максимум секторів за один виклик "sdraw". Обмеження суто проти залиття
// логу: 16 секторів - це вже 512 рядків hexdump у Serial.
static constexpr uint32_t kSdRawMaxSectors = 16;

// Повертає перший LBA розділу за його індексом у MBR (1..4), або 0, якщо
// такого розділу немає. Дозволяє звати "sdext4 2" замість "sdext4 1056768".
static uint32_t sdPartitionFirstLba(uint8_t partitionIndex) {
  const auto partitions = SDCardInspector::collectPartitions(activeSd());

  for (const auto& info : partitions) {
    if (info.index == partitionIndex) {
      return info.firstSectorLBA;
    }
  }

  return 0;
}

// "sdraw <lba> [count]" - hexdump сирих секторів картки.
void dumpSdRaw(const String& args) {
  static TLogger logger("sdraw");

  YIELD_DISPLAY_BUS();

  if (activeSd().cardType() == CARD_NONE) {
    logger.warn("SD not mounted - nothing to read (details: status sd+).");
    return;
  }

  char buffer[32];
  strlcpy(buffer, args.c_str(), sizeof(buffer));

  char* countToken = nullptr;
  char* lbaToken = strtok_r(buffer, " ", &countToken);

  if (lbaToken == nullptr || *lbaToken == '\0') {
    logger.warn("use: sdraw <lba> [count]   (e.g. sdraw 0, sdraw 1056768 2)");
    return;
  }

  const uint32_t lba = strtoul(lbaToken, nullptr, 0);  // 0 -> приймає і 0x-hex
  uint32_t count = (countToken != nullptr) ? strtoul(countToken, nullptr, 0) : 1;

  if (count == 0) {
    count = 1;
  }
  if (count > kSdRawMaxSectors) {
    logger.warn("count=%lu too large, clamped to %lu", (unsigned long)count, (unsigned long)kSdRawMaxSectors);
    count = kSdRawMaxSectors;
  }

  SDRawReader::hexdump(activeSd(), lba, count, logger);
}

// "sdext4 <partition> [sb_lba]" - розбір суперблока ext2/3/4 на розділі MBR.
//
// Другий аргумент - абсолютний LBA суперблока; потрібен, коли основний
// суперблок побитий і треба перевірити його резервну копію (адресу копії
// рахуємо з полів "per group" та "first block", які друкує ця ж команда).
void dumpSdExt4(const String& args) {
  static TLogger logger("ext4");

  YIELD_DISPLAY_BUS();

  if (activeSd().cardType() == CARD_NONE) {
    logger.warn("SD not mounted - nothing to read (details: status sd+).");
    return;
  }

  char buffer[32];
  strlcpy(buffer, args.c_str(), sizeof(buffer));

  char* sbLbaToken = nullptr;
  char* partToken = strtok_r(buffer, " ", &sbLbaToken);

  if (partToken == nullptr || *partToken == '\0') {
    logger.warn("use: sdext4 <partition 1..4> [sb_lba]   (e.g. sdext4 2)");
    return;
  }

  const uint32_t partitionIndex = strtoul(partToken, nullptr, 0);
  if (partitionIndex < 1 || partitionIndex > 4) {
    logger.warn("partition number must be 1..4 (see status sd)");
    return;
  }

  const uint32_t partitionFirstLba = sdPartitionFirstLba((uint8_t)partitionIndex);
  if (partitionFirstLba == 0) {
    logger.warn("partition %lu not found in MBR (see status sd)", (unsigned long)partitionIndex);
    return;
  }

  // Суперблок лежить за фіксованим зміщенням 1024 байти від початку РОЗДІЛУ,
  // тобто через 2 сектори по 512 байт після його першого LBA.
  const bool hasExplicitLba = (sbLbaToken != nullptr && *sbLbaToken != '\0');
  const uint32_t superblockLba = hasExplicitLba ? strtoul(sbLbaToken, nullptr, 0)
                                                : partitionFirstLba + Ext4SuperblockInspector::kSuperblockSectorOffset;

  logger.info("partition %lu: first LBA %lu, superblock at LBA %lu%s", (unsigned long)partitionIndex,
              (unsigned long)partitionFirstLba, (unsigned long)superblockLba, hasExplicitLba ? " (set manually)" : "");

  // 1024 байти суперблока = два послідовних сектори. Читаємо їх окремими
  // викликами readRAW() (його API - рівно один сектор за раз).
  uint8_t superblock[Ext4SuperblockInspector::kSuperblockSize];

  for (uint32_t i = 0; i < Ext4SuperblockInspector::kSuperblockSectorCount; ++i) {
    uint8_t* target = superblock + i * SDRawReader::kSectorSize;

    if (!SDRawReader::readSector(activeSd(), superblockLba + i, target)) {
      logger.error("failed to read LBA %lu (bad sector or out of range)", (unsigned long)(superblockLba + i));
      return;
    }
  }

  Ext4SuperblockInspector::printAll(superblock, logger);
}

// "sdbench [lba] [sectors]" - фактична швидкість послідовного raw-читання.
//
// НАВІЩО: рішення "знімати образ через плату чи ні" залежить не від
// SD_FREQ у build_flags, а від виміряних КБ/с. Команда друкує ще й прогноз
// на 1 GiB - множенням на реальний обсяг даних одразу видно, скільки годин
// (чи днів) займе копіювання.
void dumpSdBench(const String& args) {
  static TLogger logger("sdbench");

  YIELD_DISPLAY_BUS();

  if (activeSd().cardType() == CARD_NONE) {
    logger.warn("SD not mounted - nothing to read (details: status sd+).");
    return;
  }

  char buffer[32];
  strlcpy(buffer, args.c_str(), sizeof(buffer));

  char* rest = nullptr;
  char* lbaToken = strtok_r(buffer, " ", &rest);
  char* sectorsToken = strtok_r(nullptr, " ", &rest);
  char* chunkToken = strtok_r(nullptr, " ", &rest);

  const uint32_t lba = (lbaToken != nullptr && *lbaToken != '\0') ? strtoul(lbaToken, nullptr, 0) : 0;
  uint32_t sectors = (sectorsToken != nullptr) ? strtoul(sectorsToken, nullptr, 0) : 2048;
  const uint32_t chunk = (chunkToken != nullptr) ? strtoul(chunkToken, nullptr, 0) : 1;

  if (sectors == 0) {
    sectors = 2048;  // 1 MiB - достатньо, щоб усереднити накладні витрати
  }

  logger.info("reading %lu sectors (%lu KiB) from LBA %lu, in batches of %lu...", (unsigned long)sectors,
              (unsigned long)(sectors / 2), (unsigned long)lba, (unsigned long)chunk);

  RawReadStats stats;

  if (chunk > 1) {
    static ActiveBulkReader bulkReader;

    if (!bulkReader.isReady() && !bulkReader.begin(activeCardSectors())) {
      logger.error("failed to initialise the card bulk reader");
      return;
    }

    // Буфер під пачку виділяється в heap: при великому chunk впертись у
    // найбільший ВІЛЬНИЙ блок легко (LCD-буфери фрагментують купу), тому
    // друкуємо його поруч - інакше невдалий malloc виглядав би як "0 ok / 0 fail".
    logger.info("bulk mode: %lu B needed, largest free block %lu B", (unsigned long)(chunk * 512),
                (unsigned long)ESP.getMaxAllocHeap());
#if !defined(SD_USE_SDMMC)
    // pdrv є лише у SPI-реалізації: там він визначається перебором і його
    // значення - перше, що варто побачити в логу, якщо читання не пішло.
    logger.info("  pdrv=%u", (unsigned int)bulkReader.pdrv());
#endif

    stats = bulkReader.measureRead(lba, sectors, chunk);

    if (stats.sectorsOk == 0 && stats.sectorsFailed == 0) {
      logger.error("nothing was read - most likely not enough heap for the buffer");
      return;
    }
  } else {
    stats = SDRawReader::measureRead(activeSd(), lba, sectors);
  }

  logger.info("read       : %lu ok / %lu fail", (unsigned long)stats.sectorsOk, (unsigned long)stats.sectorsFailed);

  if (stats.sectorsFailed > 0) {
    logger.warn("first failed LBA: %lu", (unsigned long)stats.firstFailedLba);
  }

  if (stats.elapsedMs == 0 || stats.sectorsOk == 0) {
    logger.warn("nothing to measure (zero time or zero successful sectors)");
    return;
  }

  // Рахуємо в double: цілочисельне (sectorsOk * 512 * 1000) / elapsedMs
  // переповнює uint32 вже на кількох мегабайтах.
  const double bytes = (double)stats.sectorsOk * SDRawReader::kSectorSize;
  const double bytesPerSecond = bytes * 1000.0 / (double)stats.elapsedMs;
  const double minutesPerGiB = (1024.0 * 1024.0 * 1024.0) / bytesPerSecond / 60.0;

  logger.info("time       : %lu ms", (unsigned long)stats.elapsedMs);
  logger.info("speed      : %.1f KiB/s (%.2f MiB/s)", bytesPerSecond / 1024.0, bytesPerSecond / (1024.0 * 1024.0));
  logger.info("estimate   : %.1f min per 1 GiB (%.1f h per 50 GiB)", minutesPerGiB, minutesPerGiB * 50.0 / 60.0);
}

// "sdcrc <lba> <count> [chunk]" - CRC32 діапазону, прочитаного НА ПЛАТІ.
//
// НАВІЩО: два виклики з тими самими аргументами мусять дати той самий CRC.
// Якщо не дають - дані нестабільні, і команда одразу показує, на якому саме
// шляху: chunk=1 читає посекторно (CMD17), chunk>1 - пачками (CMD18).
// Без цієї команди нестабільність, помічену на хості, неможливо відрізнити
// від помилок транспорту.
void dumpSdCrc(const String& args) {
  static TLogger logger("sdcrc");

  YIELD_DISPLAY_BUS();

  if (activeSd().cardType() == CARD_NONE) {
    logger.warn("SD not mounted - nothing to read (details: status sd+).");
    return;
  }

  char buffer[40];
  strlcpy(buffer, args.c_str(), sizeof(buffer));

  char* rest = nullptr;
  char* lbaToken = strtok_r(buffer, " ", &rest);
  char* countToken = strtok_r(nullptr, " ", &rest);
  char* chunkToken = strtok_r(nullptr, " ", &rest);

  if (lbaToken == nullptr || *lbaToken == '\0') {
    logger.warn("use: sdcrc <lba> <count> [chunk]   (e.g. sdcrc 1056768 1024 64)");
    return;
  }

  const uint32_t lba = strtoul(lbaToken, nullptr, 0);
  uint32_t count = (countToken != nullptr) ? strtoul(countToken, nullptr, 0) : 1024;
  const uint32_t chunk = (chunkToken != nullptr) ? strtoul(chunkToken, nullptr, 0) : 1;

  if (count == 0) {
    count = 1024;
  }

  static ActiveBulkReader crcReader;

  if (!crcReader.isReady() && !crcReader.begin(activeCardSectors())) {
    logger.error("failed to determine the card pdrv");
    return;
  }

  // Четвертий аргумент вмикає читання з голосуванням: два виклики команди з
  // тими самими аргументами мусять дати той самий CRC. Це і є перевірка, що
  // голосування справді прибирає мерехтіння бітів, а не просто маскує його.
  char* passesToken = strtok_r(nullptr, " ", &rest);
  const uint32_t passes = (passesToken != nullptr) ? strtoul(passesToken, nullptr, 0) : 0;

  if (passes >= 3) {
    const uint32_t chunkSectors =
        (chunk > SdBulkReader::kMaxVotedChunkSectors) ? SdBulkReader::kMaxVotedChunkSectors : (chunk > 0 ? chunk : 1);

    uint8_t* buffer = (uint8_t*)malloc(chunkSectors * SDRawReader::kSectorSize);
    if (buffer == nullptr) {
      logger.error("no heap for a %lu B buffer", (unsigned long)(chunkSectors * 512));
      return;
    }

    SdBulkReader::VoteStats total;
    uint32_t crcVoted = 0xFFFFFFFF;
    const uint32_t startMs = millis();

    for (uint32_t done = 0; done < count;) {
      const uint32_t remaining = count - done;
      const uint32_t take = (remaining < chunkSectors) ? remaining : chunkSectors;

      const auto voteStats = crcReader.readSectorsVoted(lba + done, take, buffer, (uint8_t)passes);
      // Кожен оброблений сектор потрапляє рівно в один з чотирьох лічильників.
      // Менша сума - readSectorsVoted() вийшла раніше (не вистачило heap чи
      // рідер не готовий), і CRC по такому буферу був би вигадкою.
      if (voteStats.sectorsStable + voteStats.sectorsRecovered + voteStats.sectorsUncertain + voteStats.sectorsFailed !=
          take) {
        logger.error("LBA %lu: voted read aborted (out of heap or reader not ready) - no CRC",
                     (unsigned long)(lba + done));
        free(buffer);
        return;
      }

      total.sectorsStable += voteStats.sectorsStable;
      total.sectorsRecovered += voteStats.sectorsRecovered;
      total.sectorsUncertain += voteStats.sectorsUncertain;
      total.sectorsFailed += voteStats.sectorsFailed;
      total.bitsFixed += voteStats.bitsFixed;
      total.bitsUncertain += voteStats.bitsUncertain;

      crcVoted = SDRawReader::crc32Update(crcVoted, buffer, take * SDRawReader::kSectorSize);
      done += take;
    }

    crcVoted ^= 0xFFFFFFFF;
    free(buffer);

    logger.info("LBA %lu +%lu, majority vote over %lu passes -> CRC32 %08lX (%lu ms)", (unsigned long)lba,
                (unsigned long)count, (unsigned long)passes, (unsigned long)crcVoted,
                (unsigned long)(millis() - startMs));
    logger.info("  sectors: %lu stable / %lu recovered / %lu uncertain / %lu unreadable",
                (unsigned long)total.sectorsStable, (unsigned long)total.sectorsRecovered,
                (unsigned long)total.sectorsUncertain, (unsigned long)total.sectorsFailed);
    logger.info("  bits   : %lu fixed, %lu of them without a reliable majority", (unsigned long)total.bitsFixed,
                (unsigned long)total.bitsUncertain);
    return;
  }

  RawReadStats stats;
  const uint32_t crc = crcReader.crc32Range(lba, count, chunk, stats);

  logger.info("LBA %lu +%lu, chunk %lu -> CRC32 %08lX  (ok %lu / fail %lu, %lu ms)", (unsigned long)lba,
              (unsigned long)count, (unsigned long)chunk, (unsigned long)crc, (unsigned long)stats.sectorsOk,
              (unsigned long)stats.sectorsFailed, (unsigned long)stats.elapsedMs);
}

// "sdverify <lba> <sectors> [chunk] [passes] [delay_ms]" - чи повторюється читання.
//
// НАВІЩО: на цій картці SD-протокол помилок не показує (CRC16 кожного блоку
// валідний, fail=0), але дані щоразу різні. Команда читає діапазон кілька
// разів і друкує, які саме чанки не повторюються - тобто будує карту
// придатних до порятунку ділянок. Параметр delay_ms перевіряє, чи не зникає
// нестабільність при повільнішому читанні (просадка живлення / перегрів).
void dumpSdVerify(const String& args) {
  static TLogger logger("sdverify");

  YIELD_DISPLAY_BUS();

  if (activeSd().cardType() == CARD_NONE) {
    logger.warn("SD not mounted - nothing to read (details: status sd+).");
    return;
  }

  char buffer[64];
  strlcpy(buffer, args.c_str(), sizeof(buffer));

  char* rest = nullptr;
  char* lbaToken = strtok_r(buffer, " ", &rest);
  char* sectorsToken = strtok_r(nullptr, " ", &rest);
  char* chunkToken = strtok_r(nullptr, " ", &rest);
  char* passesToken = strtok_r(nullptr, " ", &rest);
  char* delayToken = strtok_r(nullptr, " ", &rest);

  if (lbaToken == nullptr || *lbaToken == '\0') {
    logger.warn("use: sdverify <lba> <sectors> [chunk] [passes] [delay_ms]");
    return;
  }

  const uint32_t lba = strtoul(lbaToken, nullptr, 0);
  const uint32_t sectors = (sectorsToken != nullptr) ? strtoul(sectorsToken, nullptr, 0) : 1024;
  const uint32_t chunk = (chunkToken != nullptr) ? strtoul(chunkToken, nullptr, 0) : 64;
  const uint32_t passes = (passesToken != nullptr) ? strtoul(passesToken, nullptr, 0) : 2;
  const uint32_t delayMs = (delayToken != nullptr) ? strtoul(delayToken, nullptr, 0) : 0;

  static ActiveBulkReader verifyReader;

  if (!verifyReader.isReady() && !verifyReader.begin(activeCardSectors())) {
    logger.error("failed to determine the card pdrv");
    return;
  }

  logger.info("checking LBA %lu +%lu, chunk %lu, %lu passes, %lu ms pause", (unsigned long)lba, (unsigned long)sectors,
              (unsigned long)chunk, (unsigned long)passes, (unsigned long)delayMs);

  const auto stats = verifyReader.verifyRange(lba, sectors, chunk, (uint8_t)passes, delayMs, logger);

  logger.info("chunks     : %lu (stable %lu / unstable %lu)", (unsigned long)stats.chunksTotal,
              (unsigned long)stats.chunksStable, (unsigned long)stats.chunksUnstable);
  logger.info("unreadable: %lu sectors", (unsigned long)stats.sectorsFailed);
  logger.info("time       : %lu ms", (unsigned long)stats.elapsedMs);

  if (stats.chunksUnstable > 0) {
    logger.warn("first unstable LBA: %lu", (unsigned long)stats.firstUnstableLba);
  }
}

// "sdmap [first_lba] [last_lba] [points] [sectors] [passes]" - карта деградації.
//
// Друкує по символу на точку: '.' - читається повторювано, 'x' - щоразу
// інакше (мерехтіння бітів), 'E' - не читається зовсім. Потрібна, щоб
// відрізнити локальне пошкодження від деградації всієї картки: від цього
// залежить, чи має сенс витягувати дані і які саме ділянки.
void dumpSdMap(const String& args) {
  static TLogger logger("sdmap");

  YIELD_DISPLAY_BUS();

  if (activeSd().cardType() == CARD_NONE) {
    logger.warn("SD not mounted - nothing to read (details: status sd+).");
    return;
  }

  char buffer[80];
  strlcpy(buffer, args.c_str(), sizeof(buffer));

  char* rest = nullptr;
  char* firstToken = strtok_r(buffer, " ", &rest);
  char* lastToken = strtok_r(nullptr, " ", &rest);
  char* pointsToken = strtok_r(nullptr, " ", &rest);
  char* sectorsToken = strtok_r(nullptr, " ", &rest);
  char* passesToken = strtok_r(nullptr, " ", &rest);

  const uint32_t totalSectors = activeCardSectors();

  const uint32_t firstLba = (firstToken != nullptr && *firstToken != '\0') ? strtoul(firstToken, nullptr, 0) : 0;
  const uint32_t lastLba = (lastToken != nullptr) ? strtoul(lastToken, nullptr, 0) : totalSectors;
  const uint32_t points = (pointsToken != nullptr) ? strtoul(pointsToken, nullptr, 0) : 200;
  const uint32_t sectors = (sectorsToken != nullptr) ? strtoul(sectorsToken, nullptr, 0) : 8;
  const uint32_t passes = (passesToken != nullptr) ? strtoul(passesToken, nullptr, 0) : 3;

  static ActiveBulkReader mapReader;

  if (!mapReader.isReady() && !mapReader.begin(totalSectors)) {
    logger.error("failed to determine the card pdrv");
    return;
  }

  logger.info("map LBA %lu..%lu, %lu points of %lu sectors, %lu passes", (unsigned long)firstLba,
              (unsigned long)lastLba, (unsigned long)points, (unsigned long)sectors, (unsigned long)passes);
  logger.info("'.' repeatable read, 'x' flickers, 'E' unreadable");

  const uint32_t startMs = millis();
  const uint32_t unstable = mapReader.scanMap(firstLba, lastLba, points, sectors, (uint8_t)passes, logger);

  logger.info("unstable points: %lu of %lu (%.1f%%), %lu ms", (unsigned long)unstable, (unsigned long)points,
              points > 0 ? (100.0 * unstable / points) : 0.0, (unsigned long)(millis() - startMs));
}

void registerSdReaderCommands(SerialCommander& commander) {
  commander.registerCommand("sdraw", "hexdump raw SD sectors, bypassing the filesystem: sdraw <lba> [count]",
                            [](const String& args) { dumpSdRaw(args); });

  commander.registerCommand("sdext4", "parse ext2/3/4 superblock of an MBR partition: sdext4 <1..4> [sb_lba]",
                            [](const String& args) { dumpSdExt4(args); });

  commander.registerCommand("sdbench", "measure raw sequential read speed: sdbench [lba] [sectors] [chunk]",
                            [](const String& args) { dumpSdBench(args); });

  commander.registerCommand("sdcrc",
                            "CRC32 of a sector range read on-device: sdcrc <lba> <count> [chunk] [vote_passes]",
                            [](const String& args) { dumpSdCrc(args); });

  commander.registerCommand(
      "sdverify", "check read repeatability, map unstable areas: sdverify <lba> <sectors> [chunk] [passes] [delay_ms]",
      [](const String& args) { dumpSdVerify(args); });

  commander.registerCommand("sdmap",
                            "degradation map across the card: sdmap [first_lba] [last_lba] [points] [sectors] [passes]",
                            [](const String& args) { dumpSdMap(args); });
}

#endif  // HAS_SD_WORKBENCH
