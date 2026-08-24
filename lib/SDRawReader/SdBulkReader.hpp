#pragma once

#if defined(ESP32)

#include <Print.h>

#include <cstddef>
#include <cstdint>

#include "SDRawReader.hpp"  // RawReadStats, kSectorSize

// Спільна логіка діагностики та відновлення читання SD картки, незалежна від
// того, як саме сектори дістаються з заліза.
//
// НАВІЩО БАЗОВИЙ КЛАС: усі алгоритми нижче (замір швидкості, перевірка
// повторюваності, карта деградації, читання з голосуванням) не залежать від
// інтерфейсу картки - їм потрібне лише "прочитай N секторів з LBA". Але
// шляхи до заліза різні: SPI-режим ходить через внутрішній ff_sd_read()
// (CMD18), SDMMC-режим - через власний драйвер. Тримати два набори однакових
// алгоритмів означало б виправляти кожну знахідку двічі.
//
// Віртуальний виклик тут нічого не коштує: він припадає на кожні 16-64 KiB
// прочитаних даних, а не на байт.
class SdBulkReader {
public:
  virtual ~SdBulkReader() = default;

  // Скільки секторів має сенс просити за одну операцію. 128 = 64 KiB: далі
  // виграш виходить на плато, а RAM під буфер зростає лінійно.
  static constexpr uint32_t kMaxChunkSectors = 128;

  // Менший ліміт для режиму голосування: там одночасно живуть два буфери
  // по count*512 плюс 4 KiB масиву голосів.
  static constexpr uint32_t kMaxVotedChunkSectors = 32;

  // --- реалізує конкретний нащадок ---------------------------------------
  virtual bool isReady() const = 0;
  virtual bool readSectors(uint32_t lba, uint32_t count, uint8_t *out) = 0;

  // --- спільні алгоритми --------------------------------------------------

  struct StabilityStats {
    uint32_t chunksTotal = 0;
    uint32_t chunksStable = 0;    // усі проходи дали однаковий CRC
    uint32_t chunksUnstable = 0;  // хоч один прохід розійшовся
    uint32_t sectorsFailed = 0;   // не прочитались зовсім
    uint32_t elapsedMs = 0;
    uint32_t firstUnstableLba = 0;
  };

  struct VoteStats {
    uint32_t sectorsStable = 0;
    uint32_t sectorsRecovered = 0;
    uint32_t sectorsUncertain = 0;
    uint32_t sectorsFailed = 0;
    uint32_t bitsFixed = 0;
    uint32_t bitsUncertain = 0;
  };

  // Послідовне читання без друку - для заміру фактичної швидкості.
  RawReadStats measureRead(uint32_t firstLba, uint32_t sectors, uint32_t chunkSectors);

  // CRC32 діапазону: два виклики з тими самими аргументами мусять дати той
  // самий результат. Якщо не дають - дані нестабільні.
  uint32_t crc32Range(uint32_t firstLba, uint32_t sectors, uint32_t chunkSectors,
                      RawReadStats &outStats);

  // Читає діапазон кілька разів і показує, які чанки не повторюються.
  StabilityStats verifyRange(uint32_t firstLba, uint32_t sectors, uint32_t chunkSectors,
                             uint8_t passes, uint32_t delayBetweenChunksMs, Print &out);

  // Вибіркова карта деградації по всьому діапазону: символ на точку.
  uint32_t scanMap(uint32_t firstLba, uint32_t lastLba, uint32_t points,
                   uint32_t sectorsPerPoint, uint8_t passes, Print &out);

  // Читання з побітовим голосуванням для секторів, що не повторюються.
  VoteStats readSectorsVoted(uint32_t lba, uint32_t count, uint8_t *out, uint8_t maxPasses);
};

#endif  // ESP32
