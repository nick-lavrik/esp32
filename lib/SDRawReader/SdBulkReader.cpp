#if defined(ESP32)

#include "SdBulkReader.hpp"

#include <Arduino.h>
#include <stdlib.h>
#include <string.h>

namespace {

// Голосування по бітах одного сектора.
//
// votes[i] - скільько разів i-й біт сектора прочитався як одиниця. Індекс
// біта: byteIndex * 8 + bitIndex, де bitIndex 0 - молодший біт байта.
void accumulateVotes(const uint8_t *sector, uint8_t *votes) {
  for (size_t byteIndex = 0; byteIndex < SDRawReader::kSectorSize; ++byteIndex) {
    const uint8_t value = sector[byteIndex];

    for (uint8_t bit = 0; bit < 8; ++bit) {
      if ((value >> bit) & 0x01) {
        ++votes[byteIndex * 8 + bit];
      }
    }
  }
}

}  // namespace

RawReadStats SdBulkReader::measureRead(uint32_t firstLba, uint32_t sectors,
                                          uint32_t chunkSectors) {
  RawReadStats stats;

  if (!isReady() || sectors == 0) {
    return stats;
  }

  if (chunkSectors == 0) {
    chunkSectors = 1;
  }
  if (chunkSectors > kMaxChunkSectors) {
    chunkSectors = kMaxChunkSectors;
  }

  // Буфер під пачку - у heap, не на стеку: 64 KiB на стеку FreeRTOS-таска
  // (типово 8 KiB у Arduino loop) - це гарантований stack overflow.
  const size_t bufferSize = static_cast<size_t>(chunkSectors) * SDRawReader::kSectorSize;
  uint8_t *buffer = static_cast<uint8_t *>(malloc(bufferSize));
  if (buffer == nullptr) {
    return stats;
  }

  const uint32_t startMs = millis();

  for (uint32_t done = 0; done < sectors;) {
    const uint32_t remaining = sectors - done;
    const uint32_t chunk = (remaining < chunkSectors) ? remaining : chunkSectors;
    const uint32_t lba = firstLba + done;

    if (readSectors(lba, chunk, buffer)) {
      stats.sectorsOk += chunk;
    } else {
      if (stats.sectorsFailed == 0) {
        stats.firstFailedLba = lba;
      }
      stats.sectorsFailed += chunk;
    }

    done += chunk;
  }

  stats.elapsedMs = millis() - startMs;
  free(buffer);

  return stats;
}

uint32_t SdBulkReader::crc32Range(uint32_t firstLba, uint32_t sectors, uint32_t chunkSectors,
                                     RawReadStats &outStats) {
  outStats = RawReadStats();

  if (!isReady() || sectors == 0) {
    return 0;
  }

  if (chunkSectors == 0) {
    chunkSectors = 1;
  }
  if (chunkSectors > kMaxChunkSectors) {
    chunkSectors = kMaxChunkSectors;
  }

  const size_t bufferSize = static_cast<size_t>(chunkSectors) * SDRawReader::kSectorSize;
  uint8_t *buffer = static_cast<uint8_t *>(malloc(bufferSize));
  if (buffer == nullptr) {
    return 0;
  }

  uint32_t crc = 0xFFFFFFFF;
  const uint32_t startMs = millis();

  for (uint32_t done = 0; done < sectors;) {
    const uint32_t remaining = sectors - done;
    const uint32_t chunk = (remaining < chunkSectors) ? remaining : chunkSectors;
    const uint32_t lba = firstLba + done;
    const size_t chunkBytes = static_cast<size_t>(chunk) * SDRawReader::kSectorSize;

    if (readSectors(lba, chunk, buffer)) {
      outStats.sectorsOk += chunk;
    } else {
      // Нулі замість непрочитаного - інакше CRC залежав би від сміття, що
      // лишилось у буфері від попередньої пачки, і два проходи розходились
      // би навіть на справних даних.
      memset(buffer, 0, chunkBytes);

      if (outStats.sectorsFailed == 0) {
        outStats.firstFailedLba = lba;
      }
      outStats.sectorsFailed += chunk;
    }

    crc = SDRawReader::crc32Update(crc, buffer, chunkBytes);
    done += chunk;
  }

  outStats.elapsedMs = millis() - startMs;
  free(buffer);

  return crc ^ 0xFFFFFFFF;
}

SdBulkReader::StabilityStats SdBulkReader::verifyRange(uint32_t firstLba, uint32_t sectors,
                                                             uint32_t chunkSectors, uint8_t passes,
                                                             uint32_t delayBetweenChunksMs,
                                                             Print &out) {
  StabilityStats stats;

  if (!isReady() || sectors == 0) {
    return stats;
  }

  if (chunkSectors == 0) {
    chunkSectors = 1;
  }
  if (chunkSectors > kMaxChunkSectors) {
    chunkSectors = kMaxChunkSectors;
  }
  if (passes < 2) {
    passes = 2;  // з одним проходом порівнювати нічого
  }

  const size_t bufferSize = static_cast<size_t>(chunkSectors) * SDRawReader::kSectorSize;
  uint8_t *buffer = static_cast<uint8_t *>(malloc(bufferSize));
  if (buffer == nullptr) {
    out.println("немає heap на буфер");
    return stats;
  }

  // Скільки нестабільних чанків друкувати детально. Решта - лише в
  // підсумковому рахунку: на великому діапазоні детальний список забив би
  // Serial і сам став би вузьким місцем.
  constexpr uint32_t kMaxDetailLines = 24;
  uint32_t detailLines = 0;

  const uint32_t startMs = millis();

  for (uint32_t done = 0; done < sectors;) {
    const uint32_t remaining = sectors - done;
    const uint32_t chunk = (remaining < chunkSectors) ? remaining : chunkSectors;
    const uint32_t lba = firstLba + done;
    const size_t chunkBytes = static_cast<size_t>(chunk) * SDRawReader::kSectorSize;

    ++stats.chunksTotal;

    uint32_t firstCrc = 0;
    bool isStable = true;
    bool hasFailure = false;

    for (uint8_t pass = 0; pass < passes; ++pass) {
      if (!readSectors(lba, chunk, buffer)) {
        hasFailure = true;
        memset(buffer, 0, chunkBytes);
      }

      const uint32_t crc =
          SDRawReader::crc32Update(0xFFFFFFFF, buffer, chunkBytes) ^ 0xFFFFFFFF;

      if (pass == 0) {
        firstCrc = crc;
      } else if (crc != firstCrc) {
        isStable = false;
      }

      if (delayBetweenChunksMs > 0) {
        delay(delayBetweenChunksMs);
      }
    }

    if (hasFailure) {
      stats.sectorsFailed += chunk;
    }

    if (isStable) {
      ++stats.chunksStable;
    } else {
      ++stats.chunksUnstable;

      if (stats.chunksUnstable == 1) {
        stats.firstUnstableLba = lba;
      }

      if (detailLines < kMaxDetailLines) {
        out.printf("НЕСТАБІЛЬНО: LBA %lu +%lu\n", (unsigned long)lba, (unsigned long)chunk);
        ++detailLines;
      }
    }

    done += chunk;
  }

  stats.elapsedMs = millis() - startMs;
  free(buffer);

  return stats;
}

uint32_t SdBulkReader::scanMap(uint32_t firstLba, uint32_t lastLba, uint32_t points,
                                  uint32_t sectorsPerPoint, uint8_t passes, Print &out) {
  if (!isReady() || points == 0 || lastLba <= firstLba) {
    return 0;
  }

  if (sectorsPerPoint == 0) {
    sectorsPerPoint = 8;
  }
  if (sectorsPerPoint > kMaxChunkSectors) {
    sectorsPerPoint = kMaxChunkSectors;
  }
  if (passes < 2) {
    passes = 2;
  }

  const size_t bufferSize = static_cast<size_t>(sectorsPerPoint) * SDRawReader::kSectorSize;

  uint8_t *first = static_cast<uint8_t *>(malloc(bufferSize));
  uint8_t *repeat = static_cast<uint8_t *>(malloc(bufferSize));

  if (first == nullptr || repeat == nullptr) {
    free(first);
    free(repeat);
    out.println("немає heap на буфери сканування");
    return 0;
  }

  const uint32_t step = (lastLba - firstLba) / points;
  uint32_t unstablePoints = 0;

  // Символів у рядку карти. 50 - щоб рядок з префіксом логера вкладався у
  // 128-байтовий буфер ILogger і не розривався на два.
  constexpr uint32_t kSymbolsPerLine = 50;

  char line[kSymbolsPerLine + 1];
  uint32_t symbolIndex = 0;
  uint32_t lineStartLba = firstLba;

  for (uint32_t point = 0; point < points; ++point) {
    const uint32_t lba = firstLba + point * step;

    char symbol = '.';

    if (!readSectors(lba, sectorsPerPoint, first)) {
      symbol = 'E';  // не читається зовсім
    } else {
      for (uint8_t pass = 1; pass < passes; ++pass) {
        if (!readSectors(lba, sectorsPerPoint, repeat)) {
          symbol = 'E';
          break;
        }

        if (memcmp(first, repeat, bufferSize) != 0) {
          symbol = 'x';  // читається, але щоразу інакше
          break;
        }
      }
    }

    if (symbol != '.') {
      ++unstablePoints;
    }

    line[symbolIndex++] = symbol;

    if (symbolIndex == kSymbolsPerLine || point + 1 == points) {
      line[symbolIndex] = '\0';
      // LBA початку рядка -> у GiB, щоб карту можна було читати очима.
      const uint32_t gib = (uint32_t)((uint64_t)lineStartLba * 512ULL / (1024ULL * 1024 * 1024));
      out.printf("%3lu GiB |%s|\n", (unsigned long)gib, line);

      symbolIndex = 0;
      lineStartLba = lba + step;
    }
  }

  free(repeat);
  free(first);

  return unstablePoints;
}

SdBulkReader::VoteStats SdBulkReader::readSectorsVoted(uint32_t lba, uint32_t count,
                                                             uint8_t *out, uint8_t maxPasses) {
  VoteStats stats;

  if (!isReady() || out == nullptr || count == 0) {
    return stats;
  }

  if (count > kMaxVotedChunkSectors) {
    count = kMaxVotedChunkSectors;
  }
  if (maxPasses < 3) {
    maxPasses = 3;  // менше трьох проходів голосувати нема чим
  }

  const size_t totalBytes = static_cast<size_t>(count) * SDRawReader::kSectorSize;

  uint8_t *second = static_cast<uint8_t *>(malloc(totalBytes));
  if (second == nullptr) {
    return stats;
  }

  // Крок 1: два швидких читання пачкою (CMD18) і порівняння посекторно.
  const bool firstOk = readSectors(lba, count, out);
  const bool secondOk = readSectors(lba, count, second);

  if (!firstOk && !secondOk) {
    // Пачка не читається зовсім - далі спробуємо посекторно, бо збій рідко
    // покриває всі сектори пачки.
    memset(out, 0, totalBytes);
  }

  // 4 KiB голосів на один сектор (512 байт * 8 бітів). Виділяємо один раз
  // на весь виклик, а не на кожен сектор - інакше на фрагментованій купі
  // malloc почав би відмовляти посеред операції.
  uint8_t *votes = static_cast<uint8_t *>(malloc(SDRawReader::kSectorSize * 8));
  if (votes == nullptr) {
    free(second);
    return stats;
  }

  uint8_t *singleRead = static_cast<uint8_t *>(malloc(SDRawReader::kSectorSize));
  if (singleRead == nullptr) {
    free(votes);
    free(second);
    return stats;
  }

  for (uint32_t i = 0; i < count; ++i) {
    uint8_t *target = out + i * SDRawReader::kSectorSize;
    const uint8_t *other = second + i * SDRawReader::kSectorSize;
    const uint32_t sectorLba = lba + i;

    if (firstOk && secondOk && memcmp(target, other, SDRawReader::kSectorSize) == 0) {
      ++stats.sectorsStable;
      continue;
    }

    // Крок 2: посекторне голосування. Обидва вже зроблені читання
    // враховуємо як голоси - вони така сама вибірка, як і наступні.
    memset(votes, 0, SDRawReader::kSectorSize * 8);
    uint8_t passes = 0;

    if (firstOk) {
      accumulateVotes(target, votes);
      ++passes;
    }
    if (secondOk) {
      accumulateVotes(other, votes);
      ++passes;
    }

    while (passes < maxPasses) {
      if (!readSectors(sectorLba, 1, singleRead)) {
        // Невдале читання не голосує. Виходимо одразу: якщо картка вже не
        // віддає сектор, решта проходів крутилася б даремно.
        break;
      }

      accumulateVotes(singleRead, votes);
      ++passes;
    }

    if (passes == 0) {
      memset(target, 0, SDRawReader::kSectorSize);
      ++stats.sectorsFailed;
      continue;
    }

    // Збираємо сектор з мажоритарних бітів. "Впевненим" вважаємо біт, за
    // який проголосувало щонайменше 2/3 проходів: розподіл 4:3 (57%) на
    // реальних даних теж траплявся, і мовчки видавати його за істину
    // означало б приховати ризик від того, хто потім читатиме ці файли.
    const uint8_t majorityThreshold = static_cast<uint8_t>(passes / 2);
    bool sectorUncertain = false;

    for (size_t byteIndex = 0; byteIndex < SDRawReader::kSectorSize; ++byteIndex) {
      uint8_t value = 0;

      for (uint8_t bit = 0; bit < 8; ++bit) {
        const uint8_t ones = votes[byteIndex * 8 + bit];
        const uint8_t zeros = static_cast<uint8_t>(passes - ones);

        if (ones > majorityThreshold) {
          value |= static_cast<uint8_t>(1u << bit);
        }

        // Біт мерехтів, якщо голоси не одностайні.
        if (ones != 0 && zeros != 0) {
          const uint8_t winner = (ones > zeros) ? ones : zeros;
          ++stats.bitsFixed;

          if (winner * 3 < passes * 2) {
            ++stats.bitsUncertain;
            sectorUncertain = true;
          }
        }
      }

      target[byteIndex] = value;
    }

    if (sectorUncertain) {
      ++stats.sectorsUncertain;
    } else {
      ++stats.sectorsRecovered;
    }
  }

  free(singleRead);
  free(votes);
  free(second);

  return stats;
}

#endif  // ESP32
