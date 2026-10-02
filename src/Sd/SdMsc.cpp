// HAS_SD_MSC: картка як read-only USB-накопичувач (sdmsc) і перемонтування
// після серії збоїв читання. Опис фічі - src-<env>/environment.h, розділ 2.

#include "Sd.hpp"

#if HAS_SD_MSC

#include <Arduino.h>
#include <SdMassStorage.h>

#include <Logger.hpp>
#include <TLogger.hpp>

#include "App/AppGlobals.hpp"
#include "Screen/DisplayBusYield.hpp"
#include "SdBus.hpp"

namespace {
// Читач для USB Mass Storage. Окремий від sdImageBulkReader: той живе у
// гілці HTTP-сервера, якої на цій платі немає.
static ActiveBulkReader mscBulkReader;
}  // namespace

// Перемонтування картки на прохання USB-callback.
//
// Робиться з loop(), а не з самого callback: виклик end()/begin() драйвера з
// таску TinyUSB валив систему - плата перезавантажувалась посеред знімання
// образу, а хост бачив лише "No medium found". Тут ми у головному потоці,
// який і володіє драйвером картки.
//
// Навіщо взагалі: після серії CRC-збоїв ця картка перестає відповідати
// цілком, і без перемонтування знімання образу зупинилося б на першій такій
// серії - причому хост отримував би нулі, не дізнавшись про помилку.
void remountCardIfMscAsked() {
  if (!sdMassStorageNeedsRecovery()) {
    return;
  }

  YIELD_DISPLAY_BUS();
  Logger::warn("a run of read errors - remounting the card");

  activeSd().end();
  delay(200);
  setupSD();

  mscBulkReader.begin(activeCardSectors());
  sdMassStorageInvalidatePrefetch();

  Logger::info("card remounted, cardType=%d", (int)activeSd().cardType());
}
#endif

#if defined(BOARD_ESP32_S3_LCD147)
// "sdmsc on|off|status" - віддати картку хосту як USB-накопичувач.
//
// Читач секторів передається в MSC замиканням: сам модуль не знає, чи картка
// підключена по SPI, чи по SDMMC (див. коментар до SdMscSectorReader).
void dumpSdMsc(const String& args) {
  static TLogger logger("sdmsc");

  const String action = args.length() > 0 ? args : String("status");

  if (action.equalsIgnoreCase("on")) {
    YIELD_DISPLAY_BUS();

    if (activeSd().cardType() == CARD_NONE) {
      logger.error("SD not mounted - nothing to serve to the host");
      return;
    }

    if (!mscBulkReader.isReady() && !mscBulkReader.begin(activeCardSectors())) {
      logger.error("card bulk reader failed to start");
      return;
    }

    sdMassStorageBegin(
        [](uint32_t lba, uint32_t count, uint8_t* out) { return mscBulkReader.readSectors(lba, count, out); },
        (uint32_t)activeCardSectors());
    return;
  }

  if (action.equalsIgnoreCase("off")) {
    sdMassStorageEnd();
    return;
  }

  sdMassStoragePrintStatus();
}

void registerSdMscCommands(SerialCommander& commander) {
  commander.registerCommand("sdmsc", "expose the card to the host as a read-only USB drive: sdmsc on|off|status",
                            [](const String& args) { dumpSdMsc(args); });
}

#endif  // HAS_SD_MSC
