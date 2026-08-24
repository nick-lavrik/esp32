#if defined(BOARD_ESP32_S3_LCD147)

#include <SdMassStorage.h>

#include <Arduino.h>
#include <TLogger.hpp>

#if defined(ARDUINO_USB_MODE) && ARDUINO_USB_MODE == 0
#include "USB.h"
#include "USBMSC.h"

namespace {

const TLogger logger{"usbmsc"};

USBMSC msc;
SdMscSectorReader sectorReader;
bool active = false;

uint64_t sectorsServed = 0;
uint32_t readErrors = 0;
uint32_t lastErrorLba = 0;
uint32_t writeAttempts = 0;
uint32_t consecutiveErrors = 0;
uint32_t cardRecoveries = 0;

constexpr uint32_t kSectorSize = 512;

// Скільки помилок підряд означають, що картка "залипла", а не що конкретний
// сектор битий.
//
// НАВІЩО ЦЕ ПОТРІБНО: після серії CRC-збоїв ця картка перестає відповідати
// ЗОВСІМ - перестає читатися навіть нульовий сектор, і стан не лікується
// перезавантаженням плати (перевірено: помагав лише power-cycle). Знімання
// образу триває десятки годин, і без автоматичного відновлення воно
// зупинилося б на першій же такій серії, а хост отримував би нулі замість
// решти картки, навіть не позначивши це як помилку.
constexpr uint32_t kErrorsBeforeRecovery = 24;

// Читання наперед: SDMMC віддає 6 MiB/s, а USB Full-Speed забирає близько
// 0.75 MiB/s запитами по 120 KiB. Отже картку варто читати більшими
// шматками, ніж просить хост, і віддавати наступні запити з буфера - тоді
// латентність SD-операції не додається до кожного запиту USB.
constexpr uint32_t kPrefetchSectors = 512;   // 256 KiB

uint8_t *prefetchBuffer = nullptr;
uint32_t prefetchFirstLba = 0;
uint32_t prefetchCount = 0;   // 0 - буфер порожній

// Обробник читання від хоста. bufsize зазвичай кратний розміру сектора і
// може покривати кілька секторів за раз; offset - зсув усередині сектора
// (SCSI це дозволяє, хоча на практиці буває 0).
//
// Повертає кількість байтів або -1. Збійний сектор віддаємо нулями, а не
// помилкою: обрив на одному секторі змусив би ddrescue вважати мертвою всю
// область, тоді як сусідні сектори читаються нормально.
// Прапорець для головного циклу: USB-callback НЕ має права перемонтовувати
// картку сам.
//
// ЧОМУ: перша версія викликала SD_MMC.end()/begin() прямо з onRead, тобто з
// таску TinyUSB. Це поклало систему - плата перезавантажилась посеред
// читання, а хост побачив лише "No medium found". Драйвер картки має
// підніматися з того самого потоку, що ним володіє, тому тут ми лише
// піднімаємо прапорець, а розбирається з ним loop().
volatile bool needsCardRecovery = false;

int32_t onRead(uint32_t lba, uint32_t offset, void *buffer, uint32_t bufsize) {
  uint8_t *out = static_cast<uint8_t *>(buffer);

  // Швидкий шлях: запит вирівняний по сектору і покриває цілі сектори - саме
  // так виглядають майже всі запити від ddrescue/dd.
  if (offset == 0 && (bufsize % kSectorSize) == 0) {
    const uint32_t sectors = bufsize / kSectorSize;

    // Спершу дивимось у буфер читання наперед.
    if (prefetchCount > 0 && lba >= prefetchFirstLba &&
        lba + sectors <= prefetchFirstLba + prefetchCount) {
      memcpy(out, prefetchBuffer + (lba - prefetchFirstLba) * kSectorSize, bufsize);
      sectorsServed += sectors;
      consecutiveErrors = 0;
      return static_cast<int32_t>(bufsize);
    }

    // Промах: читаємо з картки одразу великий шматок навколо запиту.
    if (prefetchBuffer != nullptr && sectors <= kPrefetchSectors) {
      if (sectorReader(lba, kPrefetchSectors, prefetchBuffer)) {
        prefetchFirstLba = lba;
        prefetchCount = kPrefetchSectors;
        memcpy(out, prefetchBuffer, bufsize);
        sectorsServed += sectors;
        consecutiveErrors = 0;
        return static_cast<int32_t>(bufsize);
      }
      // Велика пачка не пройшла - десь у ній збійний сектор. Пробуємо
      // рівно те, що просив хост, а далі посекторно.
      prefetchCount = 0;
    }

    if (sectorReader(lba, sectors, out)) {
      sectorsServed += sectors;
      consecutiveErrors = 0;
      return static_cast<int32_t>(bufsize);
    }
  }

  uint32_t done = 0;
  uint8_t sector[kSectorSize];

  while (done < bufsize) {
    const uint32_t currentLba = lba + (offset + done) / kSectorSize;
    const uint32_t inSector = (offset + done) % kSectorSize;
    const uint32_t take = min(kSectorSize - inSector, bufsize - done);

    if (sectorReader(currentLba, 1, sector)) {
      memcpy(out + done, sector + inSector, take);
      ++sectorsServed;
    } else {
      memset(out + done, 0, take);
      ++readErrors;
      ++consecutiveErrors;
      lastErrorLba = currentLba;

      if (consecutiveErrors >= kErrorsBeforeRecovery) {
        needsCardRecovery = true;   // розбереться loop(), не цей таск
      }
    }

    done += take;
  }

  return static_cast<int32_t>(bufsize);
}

// Запис заборонений. Лічильник ведемо, щоб було видно, чи хост узагалі
// намагався писати (тобто чи не змонтував він щось на запис попри read-only).
int32_t onWrite(uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t bufsize) {
  (void)lba;
  (void)offset;
  (void)buffer;
  (void)bufsize;

  ++writeAttempts;
  return -1;
}

bool onStartStop(uint8_t power_condition, bool start, bool load_eject) {
  logger.info("хост: start=%d eject=%d (power_condition=%u)", (int)start, (int)load_eject,
              (unsigned)power_condition);
  return true;
}

}  // namespace

bool sdMassStorageBegin(SdMscSectorReader reader, uint32_t totalSectors) {
  if (active) {
    logger.warn("USB MSC уже активний");
    return true;
  }

  if (!reader || totalSectors == 0) {
    logger.error("читач секторів або розмір картки не задані");
    return false;
  }

  sectorReader = reader;
  const uint32_t sectors = totalSectors;

  // Буфер читання наперед - у внутрішній RAM (MALLOC_CAP_DMA): SDMMC читає
  // через DMA і в PSRAM працювати не буде.
  if (prefetchBuffer == nullptr) {
    prefetchBuffer = static_cast<uint8_t *>(
        heap_caps_malloc(kPrefetchSectors * kSectorSize, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
  }
  logger.info("читання наперед: %s (%lu KiB)",
              prefetchBuffer ? "увімкнено" : "НЕ ВИЙШЛО (мало внутрішньої RAM)",
              (unsigned long)(kPrefetchSectors * kSectorSize / 1024));
  msc.vendorID("ESP32S3");
  msc.productID("SD-RESCUE");
  msc.productRevision("1.0");
  msc.onRead(onRead);
  msc.onWrite(onWrite);
  msc.onStartStop(onStartStop);
  msc.isWritable(false);   // хост змонтує тільки для читання
  msc.mediaPresent(true);

  if (!msc.begin(sectors, kSectorSize)) {
    logger.error("msc.begin() fail");
    return false;
  }

  USB.begin();
  active = true;

  logger.info("USB MSC піднято: %lu секторів по %lu B (%.2f GiB), READ-ONLY",
              (unsigned long)sectors, (unsigned long)kSectorSize,
              (double)sectors * kSectorSize / (1024.0 * 1024 * 1024));
  logger.info("на хості картка з'явиться як /dev/sdX (dmesg | tail)");
  return true;
}

bool sdMassStorageNeedsRecovery() {
  const bool needed = needsCardRecovery;
  needsCardRecovery = false;
  return needed;
}

void sdMassStorageInvalidatePrefetch() { prefetchCount = 0; }

void sdMassStorageEnd() {
  if (!active) {
    return;
  }
  msc.end();
  active = false;
  logger.info("USB MSC зупинено (віддано %llu секторів, збоїв %lu)",
              (unsigned long long)sectorsServed, (unsigned long)readErrors);
}

bool sdMassStorageActive() { return active; }

void sdMassStoragePrintStatus() {
  logger.info("USB MSC     : %s", active ? "активний (read-only)" : "зупинений");
  logger.info("секторів     : %llu віддано", (unsigned long long)sectorsServed);
  logger.info("збоїв читання: %lu (останній LBA %lu)", (unsigned long)readErrors,
              (unsigned long)lastErrorLba);
  logger.info("спроб запису : %lu (усі відхилені)", (unsigned long)writeAttempts);
  logger.info("відновлень   : %lu (перемонтування картки)", (unsigned long)cardRecoveries);
}

#else  // ARDUINO_USB_MODE != 0

#include <TLogger.hpp>

namespace {
const TLogger logger{"usbmsc"};
}

// Заглушки для збірки з апаратним CDC: TinyUSB (а з ним MSC) у цьому режимі
// недоступний, і краще сказати це прямо, ніж не збиратися.
bool sdMassStorageBegin(SdMscSectorReader, uint32_t) {
  logger.error("USB MSC потребує ARDUINO_USB_MODE=0 (OTG); зараз режим CDC");
  return false;
}
void sdMassStorageEnd() {}
bool sdMassStorageActive() { return false; }
void sdMassStoragePrintStatus() {
  logger.info("USB MSC: недоступний (потрібен ARDUINO_USB_MODE=0)");
}

#endif  // ARDUINO_USB_MODE

#endif  // BOARD_ESP32_S3_LCD147
