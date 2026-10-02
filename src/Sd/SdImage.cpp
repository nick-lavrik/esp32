// Домен SD_IMAGE (під HAS_SD_WORKBENCH): уся картка по HTTP для зняття
// образу - sdimg. Лише SPI-режим.

#include "Sd.hpp"

#if HAS_SD_WORKBENCH && !defined(SD_USE_SDMMC)

#include <Arduino.h>
#include <WiFi.h>

#include <Logger.hpp>
#include <SDImageServer.hpp>
#include <TLogger.hpp>

#include "App/AppGlobals.hpp"
#include "Screen/DisplayBusYield.hpp"
#include "SdBus.hpp"

// ---------------------------------------------------------------------------
// Режим знімання образу картки (команда "sdimg").
//
// НАВІЩО ОКРЕМИЙ РЕЖИМ: картка і дисплей висять на СПІЛЬНІЙ SPI-шині
// (SD_SCK=1/SD_MOSI=2 і піни панелі), а віддача образу - це години
// безперервних читань по 32 KiB. Замість того, щоб синхронізувати кожну
// транзакцію з дисплеєм, у цьому режимі loop() просто НЕ малює нічого:
// шина повністю належить картці, а порядок доступу гарантований тим, що
// і читання, і віддача в сокет ідуть з одного потоку (див. loop()).
//
// Побічний ефект: поки режим active, екран показує останній кадр -
// статичну заставку з адресою сервера, яку малюємо один раз при вмиканні.
// ---------------------------------------------------------------------------

SDImageServer sdImageServer(SDImageServerConfig{});
static ActiveBulkReader sdImageBulkReader;

bool isSdImageModeActive() { return sdImageServer.isActive(); }

// Один раз малює заставку з адресою сервера - далі дисплей не чіпаємо.
static void drawSdImageSplash() {
  display.startWrite();
  display.clear(TFT_BLACK);
  display.setTextFont(2);
  display.setTextSize(1);
  display.drawText(6, 8, "SD IMAGE MODE", TFT_GREEN);
  display.drawText(6, 32, WiFi.localIP().toString().c_str(), TFT_WHITE);
  display.drawText(6, 56, "port 8080  /sd.img", TFT_WHITE);
  display.drawText(6, 88, "display frozen:", TFT_YELLOW);
  display.drawText(6, 110, "SPI belongs to card", TFT_YELLOW);
  display.endWrite();
  display.flush();
}

void dumpSdImage(const String& args) {
  static TLogger logger("sdimg");

  const String action = args.length() > 0 ? args : String("status");

  if (action.equalsIgnoreCase("on")) {
    if (sdImageServer.isActive()) {
      logger.warn("server is already running");
      return;
    }

    YIELD_DISPLAY_BUS();

    if (activeSd().cardType() == CARD_NONE) {
      logger.error("SD not mounted - nothing to serve (details: status sd+).");
      return;
    }

    const size_t totalSectors = activeCardSectors();

    if (!sdImageBulkReader.isReady() && !sdImageBulkReader.begin(totalSectors)) {
      logger.error("failed to determine the card pdrv - cannot serve the image");
      return;
    }

    // Читач замикається на bulk-reader: пачками (CMD18) - утричі швидше за
    // посекторне читання, див. заміри в SdSpiBulkReader.hpp.
    sdImageServer.setSectorReader(
        [](uint32_t lba, uint32_t count, uint8_t* out) { return sdImageBulkReader.readSectors(lba, count, out); });
    sdImageServer.setTotalSectors(totalSectors);

    if (!sdImageServer.begin()) {
      return;
    }

    // Вимикаємо WiFi power save НА ЧАС ЗНІМАННЯ.
    //
    // Це не мікрооптимізація, а різниця в два порядки: у режимі
    // WIFI_PS_MIN_MODEM (дефолт arduino-esp32) радіо просинається лише до
    // beacon-а, тому RTT дорівнює beacon interval (~100 мс), і потік по
    // TCP просідає до одиниць KiB/s - виміряно на цій платі: 7.2 KiB/s
    // проти 1.35 MiB/s, які дає сама картка. Для передачі десятків GiB
    // сон радіо неприйнятний; повертаємо його у "sdimg off".
    WiFi.setSleep(false);
    logger.info("WiFi power save disabled (RSSI %d dBm)", WiFi.RSSI());

    // Заставку малюємо ПІСЛЯ успішного підняття сервера: інакше при відмові
    // екран залишився б замороженим ні для чого.
    drawSdImageSplash();

    logger.info("display frozen, bus handed over to the card");
    logger.info("on the host: sudo qemu-nbd --read-only --connect=/dev/nbd0 \\");
    logger.info("  'json:{\"driver\":\"raw\",\"file\":{\"driver\":\"http\",\"url\":\"http://%s:8080/sd.img\"}}'",
                WiFi.localIP().toString().c_str());
    return;
  }

  if (action.equalsIgnoreCase("off")) {
    if (!sdImageServer.isActive()) {
      logger.warn("server is not running anyway");
      return;
    }

    sdImageServer.end();

    // Повертаємо енергозбереження радіо: у звичайному режимі плата не
    // ганяє гігабайти, а WIFI_PS_NONE тримає приймач увімкненим постійно.
    WiFi.setSleep(true);
    logger.info("display unfrozen, WiFi power save restored");
    return;
  }

  // status
  logger.info("server     : %s", sdImageServer.isActive() ? "active" : "stopped");

  if (sdImageServer.isActive()) {
    logger.info("address    : http://%s:8080/sd.img", WiFi.localIP().toString().c_str());
  }

  logger.info("image      : %llu bytes", (unsigned long long)sdImageServer.totalBytes());
  logger.info("served     : %llu bytes (requests %lu)", (unsigned long long)sdImageServer.bytesServed(),
              (unsigned long)sdImageServer.requestsServed());
  logger.info("failed     : %lu sectors (last LBA %lu)", (unsigned long)sdImageServer.badSectors(),
              (unsigned long)sdImageServer.lastBadLba());
}

void sdImageHandleClient() { sdImageServer.handleClient(); }

void registerSdImageCommands(SerialCommander& commander) {
  commander.registerCommand("sdimg",
                            "serve the whole card over HTTP for imaging (freezes the display): sdimg on|off|status",
                            [](const String& args) { dumpSdImage(args); });
}

#endif  // HAS_SD_WORKBENCH && !SD_USE_SDMMC
