// Базове SD: монтування, "status sd"/"sd+", картка на вкладці System.
// Лише BOARD_HAS_SD; важкий інструментарій - SdProbe/SdReader/SdImage
// (HAS_SD_WORKBENCH), USB-диск - SdMsc (HAS_SD_MSC).

#include <Logger.hpp>

#include "Sd.hpp"

#if BOARD_HAS_SD
#include <Arduino.h>

#include <Logger.hpp>
#include <TLogger.hpp>

#include "App/AppGlobals.hpp"
#include "Screen/DisplayBusYield.hpp"
#include "SdBus.hpp"
#include "SizeFormatter.hpp"
#endif

void setupSD() {
#if BOARD_HAS_SD
// SD_USE_SDMMC - src/Sd/Sd.hpp.
#if defined(SD_USE_SDMMC)
  // SD_MMC (4-bit): піни задаються з build_flags (SD_D0/D1/D2/D3/CLK/CMD).
  if (!SD_MMC.setPins(SD_CLK, SD_CMD, SD_D0, SD_D1, SD_D2, SD_D3)) {
    Logger::error("SD_MMC.setPins() fail.");
    return;
  }

  const int maxAttempts = 3;
  for (int attempt = 1; attempt <= maxAttempts; ++attempt) {
    if (SD_MMC.begin("/sdcard", /*mode1bit=*/false)) {
      Logger::info("SD_MMC init done (%d/%d)", attempt, maxAttempts);
      return;
    }
    delay(100);
  }

  Logger::error("SD_MMC init fail.");
#else
  // --- SPI-гілка ---------------------------------------------------------
  // На esp32-c6-lcd096 шина СПІЛЬНА з дисплеєм (SCK=7, MOSI=6), окремі
  // CS: 4 (SD) / 14 (LCD). setupSD() навмисно викликається ПЕРЕД
  // setupDisplay() (див. setup()), тому на цей момент TFT_CS ще НЕ
  // сконфігурований драйвером дисплея і висить плаваючим входом. ST7735
  // write-only і сам MISO не тягне, але поки його CS не підтягнутий у
  // HIGH, він приймає весь init-трафік картки як власні команди - і
  // залишає дисплей у невизначеному стані. Тому деактивуємо всі інші CS
  // шини ДО першого такту SCK.
#if defined(TFT_CS) && (TFT_CS >= 0)
  pinMode(TFT_CS, OUTPUT);
  digitalWrite(TFT_CS, HIGH);
#endif

  // CS картки має бути HIGH ще до SPI.begin(): за специфікацією SD картка
  // переходить у SPI-режим лише побачивши >=74 такти при деактивованому CS.
  pinMode(SD_CS, OUTPUT);
  digitalWrite(SD_CS, HIGH);

  // Внутрішній підтяг на MISO: на дешевих платах зовнішнього резистора на
  // лінії DO картки часто немає, і поки картка не вибрана, лінія "висить" -
  // контролер читає сміття замість 0xFF і CMD0/CMD8 не проходять.
  pinMode(SD_MISO, INPUT_PULLUP);

  SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);

  Logger::info("SD (SPI): CS=%d SCK=%d MOSI=%d MISO=%d", SD_CS, SD_SCK, SD_MOSI, SD_MISO);

  // Сходинки частот: СПОЧАТКУ робоча SD_FREQ, і лише якщо вона не
  // піднялась - 400 кГц (частота ініціалізації за специфікацією SD) як
  // запасний варіант. Порядок принциповий: перша успішна сходинка стає
  // робочою частотою шини на весь сеанс, тому 400 кГц першою означала б
  // вдесятеро повільніший SD навіть там, де 4 МГц працюють.
  // Якщо в лозі видно fail на SD_FREQ і успіх на 400 кГц - проблема в
  // якості шини (спільна з дисплеєм), а не в пінах чи картці.
  const uint32_t freqs[] = {(uint32_t)SD_FREQ, 400000UL};
  const int maxAttempts = 3;

  for (uint32_t freq : freqs) {
    for (int attempt = 1; attempt <= maxAttempts; ++attempt) {
      if (SD.begin(SD_CS, SPI, freq)) {
        Logger::info("SD card init done (%u Hz, attempt %d/%d)", (unsigned)freq, attempt, maxAttempts);
        return;
      }
      delay(100);
    }
    Logger::warn("SD init fail @ %u Hz", (unsigned)freq);
  }

  Logger::error("SD init fail. Check: card inserted? FAT32/FAT16 (NOT exFAT, NOT >32GB)? pins?");
#endif
#else
  Logger::warn("SD disabled.");
#endif

  return;
}

#if BOARD_HAS_SD

static void dumpSDlistDir(const char* dirname, uint8_t levels) {
  Logger::info("Directory contents: %s", dirname);

  File root = activeSd().open(dirname);
  if (!root || !root.isDirectory()) {
    Logger::info("  (failed to open directory)");
    return;
  }

  File file = root.openNextFile();
  int maxFiles = 50;
  while (file && --maxFiles) {
    if (file.isDirectory()) {
      Logger::info("  DIR : %-30s       ****", file.name());
      if (levels) {
        dumpSDlistDir(file.path(), levels - 1);
      }
    } else {
      Logger::info("  FILE: %-30s SIZE: %u", file.name(), file.size());
    }
    file = root.openNextFile();
  }
  if (file && !maxFiles) {
    Logger::info("  ...");
  }
}

// Спільна для dumpSDInfo() (serial) і getSdCardInfo() (WebSystemModule,
// вкладка System порталу) - одна таблиця замість двох, що розійдуться.
static const char* sdCardTypeName(uint8_t cardType) {
  if (cardType == CARD_MMC) return "MMC";
  if (cardType == CARD_SD) return "SDSC";
  if (cardType == CARD_SDHC) return "SDHC";
  return "UnknownType";
}

void dumpSDInfo() {
  // 1. Деактивируем выбор других устройств на шине
  // digitalWrite(15, HIGH); // Отключаем TFT_CS
  // digitalWrite(33, HIGH); // Отключаем TOUCH_CS
  // digitalWrite(5, HIGH);  // SD_CS = HIGH (пока отключен)

  YIELD_DISPLAY_BUS();

  Logger::info("========= SD Card Info =====================================");

  uint8_t cardType = activeSd().cardType();

  if (cardType == CARD_NONE) {
    Logger::info("Card not found (or type not detected).");
    Logger::info("============================================================");
    return;
  }

  Logger::info("Card found.");

  // Виводимо тип для деталізації
  Logger::info("Card type: %s", sdCardTypeName(cardType));

  Logger::info("------------------------------------------------------------");
  dumpSDlistDir("/", 2);
  Logger::info("------------------------------------------------------------");

  // Виводимо розмір картки
  // uint64_t cardSize = activeSd().cardSize() / (1024 * 1024);
  // Serial.printf(F("Розмір картки: %llu MB\n"), cardSize);
  Logger::info("Card size: %s", SizeFormatter::format(activeSd().cardSize()).c_str());
  Logger::info("Used: %s (%.2f%%)", SizeFormatter::format(activeSd().usedBytes()).c_str(),
               activeSd().usedBytes() * 100.0 / activeSd().cardSize());
  Logger::info("Free:  %s (%.2f%%)", SizeFormatter::format(activeSd().cardSize() - activeSd().usedBytes()).c_str(),
               (activeSd().cardSize() - activeSd().usedBytes()) * 100.0 / activeSd().cardSize());

  Logger::info("============================================================");
}

#if HAS_WEB_PORTAL
// Для вкладки System порталу (WebSystemModule, конструюється в
// src/App/AppGlobals.cpp) - та сама activeSd(), що й dumpSDInfo() вище.
bool getSdCardInfo(WebSystemSdInfo& out) {
  const uint8_t cardType = activeSd().cardType();
  out.present = cardType != CARD_NONE;
  if (!out.present) return true;

  out.cardType = sdCardTypeName(cardType);
  out.sizeBytes = activeSd().cardSize();
  out.usedBytes = activeSd().usedBytes();
  return true;
}
#endif

// "status sd": тип картки, розмір і MBR-розділи (SDCardInspector).
void printSdStatus() {
  // Не static: команда рідкісна, а static TLogger - 148 Б .bss назавжди.
  TLogger logger("flash");
  YIELD_DISPLAY_BUS();
  // ОБОВ'ЯЗКОВА перевірка перед readRAW(): на відміну від cardType(), який
  // чесно віддає CARD_NONE при _pdrv == 0xFF, SDFS::readRAW() передає цей
  // самий 0xFF прямо в ff_sd_read(), а той робить s_cards[pdrv] БЕЗ
  // перевірки меж (масив на FF_VOLUMES елементів). Читання за межами
  // масиву + розіменування сміттєвого вказівника = миттєвий reset плати.
  // Саме так "status sd" на незмонтованій картці перезавантажував пристрій.
  if (activeSd().cardType() == CARD_NONE) {
    Logger::warn("SD not mounted - nothing to read (details: status sd+).");
  } else {
    SDCardInspector::printAll(activeSd(), logger);
  }
}

void registerSdCommands(SerialCommander& commander) {
#if HAS_SD_WORKBENCH
#if !defined(SD_USE_SDMMC)
  registerSdProbeCommands(commander);
#endif
  registerSdReaderCommands(commander);
#if !defined(SD_USE_SDMMC)
  registerSdImageCommands(commander);
#endif
#endif
#if HAS_SD_MSC
  registerSdMscCommands(commander);
#endif
  (void)commander;
}

#endif  // BOARD_HAS_SD
