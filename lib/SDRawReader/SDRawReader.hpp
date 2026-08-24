#pragma once

#include <Arduino.h>  // delay() у retry-паузах
#include <Print.h>

#include <cstddef>
#include <cstdint>


// Результат вимірювання швидкості послідовного raw-читання.
struct RawReadStats {
  uint32_t sectorsOk = 0;      // успішно прочитані сектори
  uint32_t sectorsFailed = 0;  // сектори, не прочитані навіть після retries
  uint32_t elapsedMs = 0;      // сумарний час читання
  uint32_t firstFailedLba = 0;  // LBA першого збійного сектора (0 - збоїв не було)
};

// Побайтове (raw) читання секторів SD картки в обхід файлової системи.
//
// НАВІЩО окремо від SDCardInspector: той читає рівно сектор 0 і розбирає
// MBR. Тут потрібне довільне читання будь-якого LBA — для розділів, чия
// ФС узагалі не підтримується ESP32 (ext4, XFS), та для порятунку даних
// з фізично збійних карток.
//
// Працює як з fs::SDFS (SD.h, SPI-режим), так і з fs::SDMMCFS (SD_MMC.h) —
// доступ через шаблон з тієї ж причини, що й у SDCardInspector: обидва
// класи мають readRAW()/sectorSize()/numSectors(), але не мають спільного
// базового інтерфейсу з цими методами.
//
// КРИТИЧНО: перед будь-яким викликом readRAW() зовнішній код ЗОБОВ'ЯЗАНИЙ
// перевірити cardType() != CARD_NONE. На незмонтованій картці _pdrv == 0xFF
// потрапляє прямо в ff_sd_read(), який індексує s_cards[pdrv] без перевірки
// меж — плата ресетиться (та сама пастка, що описана для "status sd" у
// main.cpp).
class SDRawReader {
public:
  static constexpr size_t kSectorSize = 512;  // LBA-сектор SD/SDHC/SDXC
  static constexpr uint8_t kDefaultRetries = 3;

  // Читає один сектор з повторними спробами.
  //
  // Повтори тут не формальність: на картці з деградованою flash-пам'яттю
  // контролер часто віддає сектор з другої-третьої спроби (внутрішній
  // ECC/read-retry), тому один невдалий readRAW() ще не означає, що сектор
  // мертвий. `out` має бути >= kSectorSize байтів.
  template <typename SDCardT>
  static bool readSector(SDCardT &sdInstance, uint32_t lba, uint8_t *out,
                         uint8_t retries = kDefaultRetries) {
    if (out == nullptr || retries == 0) {
      return false;
    }

    for (uint8_t attempt = 0; attempt < retries; ++attempt) {
      if (sdInstance.readRAW(out, lba)) {
        return true;
      }
      delay(20);  // дати контролеру картки час на внутрішній read-retry
    }

    return false;
  }

  // Друкує hexdump `count` секторів починаючи з `firstLba`.
  //
  // Формат рядка: "<offset>  <16 байт hex>  |<ascii>|", перед кожним
  // сектором — заголовок з його LBA та байтовим зміщенням на картці.
  // Збійний сектор не перериває дамп: рядок "read FAIL" і перехід до
  // наступного (для порятунку даних важливо знати, ЯКІ саме сектори биті).
  template <typename SDCardT>
  static void hexdump(SDCardT &sdInstance, uint32_t firstLba, uint32_t count, Print &out,
                      uint8_t retries = kDefaultRetries) {
    if (count == 0) {
      count = 1;
    }

    uint8_t sector[kSectorSize];

    for (uint32_t i = 0; i < count; ++i) {
      const uint32_t lba = firstLba + i;

      // Байтове зміщення на картці не влазить у 32 біти вже на 2 ГБ —
      // рахуємо в 64 бітах, інакше для великих карток вивелась би нісенітниця.
      const uint64_t byteOffset = static_cast<uint64_t>(lba) * kSectorSize;

      out.printf("--- LBA %lu (byte %llu) ---\n", static_cast<unsigned long>(lba),
                 static_cast<unsigned long long>(byteOffset));

      if (!readSector(sdInstance, lba, sector, retries)) {
        out.printf("read FAIL after %u attempt(s)\n", static_cast<unsigned int>(retries));
        continue;
      }

      printHexBlock(sector, kSectorSize, out);
    }
  }

  // Послідовно читає `count` секторів, нічого не друкуючи, і повертає
  // статистику з часом.
  //
  // НАВІЩО: перед копіюванням десятків гігабайт через плату треба знати
  // фактичну швидкість шини. Теоретична (SD_FREQ / 8) не має нічого
  // спільного з реальною: на кожен сектор іде окрема SD-команда CMD17 з
  // очікуванням токена даних, і саме ці накладні витрати, а не частота,
  // визначають результат на дрібних читаннях.
  //
  // Збійний сектор не перериває вимірювання - для картки, що вмирає,
  // важливо знати і швидкість, і щільність збоїв за один прохід.
  template <typename SDCardT>
  static RawReadStats measureRead(SDCardT &sdInstance, uint32_t firstLba, uint32_t count,
                                  uint8_t retries = kDefaultRetries) {
    RawReadStats stats;

    if (count == 0) {
      return stats;
    }

    uint8_t sector[kSectorSize];
    const uint32_t startMs = millis();

    for (uint32_t i = 0; i < count; ++i) {
      const uint32_t lba = firstLba + i;

      if (readSector(sdInstance, lba, sector, retries)) {
        ++stats.sectorsOk;
      } else {
        if (stats.sectorsFailed == 0) {
          stats.firstFailedLba = lba;
        }
        ++stats.sectorsFailed;
      }
    }

    stats.elapsedMs = millis() - startMs;
    return stats;
  }

  // CRC32 (той самий поліном, що в zlib/PNG - 0xEDB88320, відображений).
  //
  // НАВІЩО ВЛАСНА РЕАЛІЗАЦІЯ: потрібен спосіб порівняти ДВА читання одного
  // й того самого діапазону, не витягуючи дані з плати. Nibble-таблиця на
  // 16 записів - компроміс: у 8 разів швидша за побітову і не займає
  // кілобайт flash, як повна таблиця на 256 записів.
  static uint32_t crc32Update(uint32_t crc, const uint8_t *data, size_t length) {
    static constexpr uint32_t kNibbleTable[16] = {
        0x00000000, 0x1DB71064, 0x3B6E20C8, 0x26D930AC, 0x76DC4190, 0x6B6B51F4,
        0x4DB26158, 0x5005713C, 0xEDB88320, 0xF00F9344, 0xD6D6A3E8, 0xCB61B38C,
        0x9B64C2B0, 0x86D3D2D4, 0xA00AE278, 0xBDBDF21C};

    for (size_t i = 0; i < length; ++i) {
      crc ^= data[i];
      crc = (crc >> 4) ^ kNibbleTable[crc & 0x0F];
      crc = (crc >> 4) ^ kNibbleTable[crc & 0x0F];
    }

    return crc;
  }

  // Друкує довільний буфер у тому ж форматі, що й hexdump() (16 байт у рядку).
  // Виділено окремо, щоб дамп уже прочитаної області (напр. суперблока ext4)
  // виглядав так само, як дамп прямо з картки.
  static void printHexBlock(const uint8_t *data, size_t length, Print &out) {
    for (size_t offset = 0; offset < length; offset += 16) {
      const size_t lineLength = (length - offset < 16) ? (length - offset) : 16;

      // Один рядок збираємо в локальний буфер і друкуємо ОДНИМ printf:
      // логер проєкту розбиває вивід по '\n' і додає префікс "[I][tag ]"
      // на кожен рядок, тому по-байтовий друк дав би 16 префіксів у рядку.
      char line[80];
      int pos = snprintf(line, sizeof(line), "%04X  ", static_cast<unsigned int>(offset));

      for (size_t i = 0; i < 16; ++i) {
        if (i < lineLength) {
          pos += snprintf(line + pos, sizeof(line) - pos, "%02X ", data[offset + i]);
        } else {
          pos += snprintf(line + pos, sizeof(line) - pos, "   ");
        }
      }

      pos += snprintf(line + pos, sizeof(line) - pos, " |");
      for (size_t i = 0; i < lineLength; ++i) {
        const uint8_t byte = data[offset + i];
        const char printable = (byte >= 0x20 && byte < 0x7F) ? static_cast<char>(byte) : '.';
        pos += snprintf(line + pos, sizeof(line) - pos, "%c", printable);
      }
      snprintf(line + pos, sizeof(line) - pos, "|");

      out.println(line);
    }
  }

private:
  SDRawReader() = delete;  // лише статичні методи
};
