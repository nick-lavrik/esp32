#pragma once

// Внутрішній заголовок модуля src/Sd: вибір шини картки (SPI чи SDMMC) і
// спільне для доменів SdCard/SdProbe/SdReader/SdImage/SdMsc. Решта src/
// включає лише Sd.hpp.

#include "Sd.hpp"

#if BOARD_HAS_SD

#include <SerialCommander.hpp>

#if defined(SD_USE_SDMMC)
// Ця плата підключає TF-карту через SD_MMC (4-bit: D0/D1/D2/D3/CLK/CMD),
// а не через SPI (CS/MOSI/MISO/SCK), як інші плати проєкту.
#include <SD_MMC.h>
#else
#include <SD.h>
#endif
#include <SDCardInspector.hpp>

// Активна картка - SD або SD_MMC, той самий публічний API (cardType()/
// cardSize()/readRAW()/...). Функція, а не макрос: колишній "#define
// ACTIVE_SD SD_MMC" доводилось #undef-ити одразу після використання, бо
// глобальна текстова підміна імені SD ламала сторонні бібліотеки, що
// транзитивно включають <SD.h> (ловилось на ESP Mail Client -> MB_FS.h).
#if defined(SD_USE_SDMMC)
inline fs::SDMMCFS& activeSd() { return SD_MMC; }
#else
inline fs::SDFS& activeSd() { return SD; }
#endif

#if HAS_SD_WORKBENCH || HAS_SD_MSC
#include <SDRawReader.hpp>
// Діагностика картки (sdbench/sdcrc/sdverify/sdmap, sdimg, sdmsc) працює в
// обох режимах через спільний базовий клас SdBulkReader; різниця лише в тому,
// як сектори дістаються з заліза. ActiveBulkReader - псевдонім потрібної
// реалізації.
#if defined(SD_USE_SDMMC)
#include <SdMmcBulkReader.hpp>
using ActiveBulkReader = SdMmcBulkReader;
#else
#include <SdSpiBulkReader.hpp>
using ActiveBulkReader = SdSpiBulkReader;
#endif

// Кількість секторів УСІЄЇ картки.
//
// НАВІЩО НЕ numSectors(): у SDFS (SPI-режим) він повертає розмір картки, а в
// SDMMCFS - розмір ЗМОНТОВАНОЇ файлової системи, тобто тут 512-мегабайтного
// boot-розділу (SD_MMC.cpp: numSectors() = totalBytes() / sector_size, а
// totalBytes() питає f_getfree про змонтований том). Через це на SD_MMC усе
// за межами першого розділу відкидалося як вихід за межі картки - разом з
// ext4-розділом, по який ми й прийшли.
//
// cardSize() в обох класах рахується з CSD-регістра самої картки, тому дає
// однаковий і правильний результат незалежно від режиму та від того, що саме
// змонтовано.
inline uint64_t activeCardSectors() { return activeSd().cardSize() / SDRawReader::kSectorSize; }
#endif  // HAS_SD_WORKBENCH || HAS_SD_MSC

// Реєстрація команд по доменах - збирає registerSdCommands() (SdCard.cpp).
#if HAS_SD_WORKBENCH
void registerSdReaderCommands(SerialCommander& commander);
#if !defined(SD_USE_SDMMC)
void registerSdProbeCommands(SerialCommander& commander);
void registerSdImageCommands(SerialCommander& commander);
#endif
#endif
#if HAS_SD_MSC
void registerSdMscCommands(SerialCommander& commander);
#endif

#endif  // BOARD_HAS_SD
