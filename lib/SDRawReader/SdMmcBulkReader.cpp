#if defined(ESP32)

// Плати без SDMMC-периферії (напр. ESP32-C6) цього класу не мають зовсім:
// там і <SD_MMC.h> порожній. SOC_SDMMC_HOST_SUPPORTED - ознака від ESP-IDF,
// а не від конкретної плати, тому guard не треба правити під нові плати.
#include <soc/soc_caps.h>
#if defined(SOC_SDMMC_HOST_SUPPORTED) && SOC_SDMMC_HOST_SUPPORTED

#include "SdMmcBulkReader.hpp"

#include <Arduino.h>
#include <SD_MMC.h>
#include <string.h>

// Багатосекторне читання тому FatFs. У ESP-IDF стандартна FatFs-функція
// disk_read перейменована через ffconf.h (#define disk_read ff_disk_read),
// тому символ називається так; сигнатура - канонічна FatFs.
//
// ЧОМУ ЦЕ ПОТРІБНО: публічний SD_MMC.readRAW() жорстко передає count = 1,
// тобто на кожні 512 байт іде окрема команда. Через USB це дало 539 KiB/s,
// хоча сама 4-bit шина здатна на кратно більше.
//
// ЧОМУ ЦЕ БЕЗПЕЧНО РОБИТИ САМЕ ТАК: номер драйвера не вгадується "на віру" -
// begin() читає нульовий сектор обома шляхами і порівнює байти. Якщо
// збігаються, отже pdrv справді веде до цієї картки; якщо ні (або читання не
// вдалося) - багатосекторний шлях просто не вмикається, і клас працює як
// раніше. Розіменування чужого драйвера в ESP-IDF не перевіряється, тому
// покладатися лише на "зазвичай це нуль" не можна.
extern "C" {
int ff_disk_read(unsigned char pdrv, unsigned char *buff, uint32_t sector, unsigned int count);
}

namespace {
constexpr int kFatFsResOk = 0;  // FatFs RES_OK
constexpr unsigned char kProbePdrv = 0;  // SD_MMC монтується першим томом
}  // namespace

bool SdMmcBulkReader::begin(size_t expectedSectors) {
  _isReady = false;
  _totalSectors = 0;

  if (expectedSectors == 0 || SD_MMC.cardType() == CARD_NONE) {
    return false;
  }

  _totalSectors = static_cast<uint32_t>(expectedSectors);
  _isReady = true;

  // Перевірка багатосекторного шляху: той самий сектор, прочитаний двома
  // способами, мусить дати ті самі байти. Нульовий сектор для цього
  // годиться - це MBR, він на цій картці читається стабільно.
  uint8_t viaPublicApi[SDRawReader::kSectorSize];
  uint8_t viaFatFs[SDRawReader::kSectorSize];

  if (SD_MMC.readRAW(viaPublicApi, 0) &&
      ff_disk_read(kProbePdrv, viaFatFs, 0, 1) == kFatFsResOk &&
      memcmp(viaPublicApi, viaFatFs, SDRawReader::kSectorSize) == 0) {
    _canBulk = true;
  }

  return true;
}

bool SdMmcBulkReader::readSectors(uint32_t lba, uint32_t count, uint8_t *out) {
  if (!_isReady || out == nullptr || count == 0) {
    return false;
  }

  // Вихід за межі картки відсікаємо самі: SDMMCFS::readRAW() передає сектор
  // у драйвер без перевірки, а читання за останнім сектором на різних
  // картках поводиться по-різному - від помилки до тихого сміття.
  if (lba > _totalSectors || count > _totalSectors - lba) {
    return false;
  }

  if (_canBulk && count > 1) {
    return ff_disk_read(kProbePdrv, out, lba, count) == kFatFsResOk;
  }

  for (uint32_t i = 0; i < count; ++i) {
    if (!SD_MMC.readRAW(out + i * SDRawReader::kSectorSize, lba + i)) {
      return false;
    }
  }

  return true;
}

#endif  // SOC_SDMMC_HOST_SUPPORTED
#endif  // ESP32
