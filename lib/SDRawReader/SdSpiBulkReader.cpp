#if defined(ESP32)

#include "SdSpiBulkReader.hpp"

#include <Arduino.h>
#include <stdlib.h>
#include <string.h>

// Публічна частина драйверного API arduino-esp32 (libraries/SD/src/sd_diskio.h).
// Обидві функції безпечні при невалідному pdrv - саме тому ідентифікація
// драйвера в begin() будується на них, а не на ff_sd_read().
//
// БЕЗ extern "C": sd_diskio.cpp компілюється як C++ і його заголовок
// extern "C" не оголошує, тому символи в бібліотеці мають C++-манглінг
// (перевірено: nm sd_diskio.cpp.o -> _Z18sdcard_num_sectorsh). Обгортка
// extern "C" тут ламала б лінкування.
uint32_t sdcard_num_sectors(uint8_t pdrv);
uint32_t sdcard_sector_size(uint8_t pdrv);

// А це вже внутрішній символ sd_diskio.cpp: у sd_diskio.h його немає.
// Підпис звірений з framework-arduinoespressif32 (core 3.3.9):
//   DRESULT ff_sd_read(uint8_t pdrv, uint8_t* buffer, DWORD sector, UINT count)
//
// Типи мусять збігтися ДО БІТА, бо вони входять у мангловане ім'я
// (_Z10ff_sd_readhPhmj: h=uint8_t, Ph=uint8_t*, m=unsigned long, j=unsigned int).
// Саме тому sector оголошений як unsigned long, а не uint32_t: на riscv32
// uint32_t - це unsigned int ('j'), і з ним лінкер символу не знайде, хоча
// за розміром типи ідентичні. DRESULT з FatFs - enum, за ABI повертається
// як int.
int ff_sd_read(uint8_t pdrv, uint8_t *buffer, unsigned long sector, unsigned int count);

namespace {

constexpr int kResOk = 0;  // FatFs RES_OK

// Скільки значень pdrv перебирати. FF_VOLUMES в ESP-IDF за замовчуванням 2;
// беремо із запасом, бо sdcard_num_sectors() сама відсіює pdrv >= FF_VOLUMES
// і повертає 0 - вихід за межі масиву тут неможливий.
constexpr uint8_t kProbeVolumeCount = 4;

}  // namespace

bool SdSpiBulkReader::begin(size_t expectedSectors) {
  _isReady = false;
  _pdrv = 0xFF;

  if (expectedSectors == 0) {
    return false;  // картка не змонтована - шукати нічого
  }

  for (uint8_t candidate = 0; candidate < kProbeVolumeCount; ++candidate) {
    // Збіг ОДРАЗУ за двома незалежними полями: кількість секторів у цієї
    // конкретної картки і розмір сектора. Порожній слот дає 0 в обох.
    if (sdcard_num_sectors(candidate) != expectedSectors) {
      continue;
    }
    if (sdcard_sector_size(candidate) != SDRawReader::kSectorSize) {
      continue;
    }

    _pdrv = candidate;
    _isReady = true;
    return true;
  }

  return false;
}

bool SdSpiBulkReader::readSectors(uint32_t lba, uint32_t count, uint8_t *out) {
  if (!_isReady || out == nullptr || count == 0) {
    return false;
  }

  return ff_sd_read(_pdrv, out, static_cast<unsigned long>(lba),
                    static_cast<unsigned int>(count)) == kResOk;
}

#endif  // ESP32
