#pragma once

// SD-картка: публічний API модуля src/Sd для решти src/.
//
//   setupSD();                          // з setup(), ДО setupDisplay()
//   registerSdCommands(commandHandler); // з setupSerialCommander()
//   remountCardIfMscAsked();            // щоітерації loop()
//   if (isSdImageModeActive()) { sdImageHandleClient(); ... }
//
// Три рівні, кожен - свій прапорець (src-<env>/environment.h, розділ 2):
//   BOARD_HAS_SD     - слот є: монтування, 'status sd'/'status sd+', картка
//                      на вкладці System (SdCard.cpp);
//   HAS_SD_WORKBENCH - важкий інструментарій: домени SD_PROBE (SdProbe.cpp),
//                      SD_READER (SdReader.cpp), SD_IMAGE (SdImage.cpp);
//   HAS_SD_MSC       - картка як USB-накопичувач (SdMsc.cpp).
// Без прапорця функції нижче - порожні inline, тож виклики лишаються без #if.

#include <SerialCommander.hpp>

#include "features.h"

#if HAS_SD_WORKBENCH && !BOARD_HAS_SD
#error "HAS_SD_WORKBENCH=1 requires BOARD_HAS_SD=1"
#endif
#if HAS_SD_MSC && !BOARD_HAS_SD
#error "HAS_SD_MSC=1 requires BOARD_HAS_SD=1"
#endif
// MSC - наша фіча поверх можливості ЧИПА: USB-OTG (TinyUSB device). Є на
// S2/S3; на C3/C6 лише USB-Serial-JTAG, на якому накопичувач не зробити.
#if HAS_SD_MSC
#include <soc/soc_caps.h>
#if !SOC_USB_OTG_SUPPORTED
#error "HAS_SD_MSC=1 requires a chip with USB-OTG (SOC_USB_OTG_SUPPORTED), e.g. ESP32-S3"
#endif
#endif

// SD_USE_SDMMC - внутрішній прапорець: SDMMC-режим є лише на платі з таким
// роз'ємом, і лише якщо його явно не відключили через SD_FORCE_SPI.
#if BOARD_HAS_SD && defined(BOARD_ESP32_S3_LCD147) && !defined(SD_FORCE_SPI)
#define SD_USE_SDMMC 1
#endif

#if HAS_WEB_PORTAL && BOARD_HAS_SD
#include <WebSystemModule.hpp>
#endif

// Без BOARD_HAS_SD лише логує "SD disabled".
void setupSD();

#if BOARD_HAS_SD
void registerSdCommands(SerialCommander& commander);
void printSdStatus();  // 'status sd'
void dumpSDInfo();     // 'status sd+'
#if HAS_WEB_PORTAL
// Провайдер картки для вкладки System (WebSystemModule, src/App/AppGlobals.cpp).
bool getSdCardInfo(WebSystemSdInfo& out);
#endif
#else
inline void registerSdCommands(SerialCommander&) {}
#endif

// Режим 'sdimg': поки активний, loop() не малює (шина належить картці).
#if HAS_SD_WORKBENCH && !defined(SD_USE_SDMMC)
bool isSdImageModeActive();
void sdImageHandleClient();
#else
inline bool isSdImageModeActive() { return false; }
inline void sdImageHandleClient() {}
#endif

// Перемонтування картки на прохання USB-стеку (MSC) - з loop(), не з таску TinyUSB.
#if HAS_SD_MSC
void remountCardIfMscAsked();
#else
inline void remountCardIfMscAsked() {}
#endif
