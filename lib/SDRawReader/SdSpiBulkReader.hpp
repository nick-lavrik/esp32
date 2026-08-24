#pragma once

#if defined(ESP32)

#include "SdBulkReader.hpp"

// Багатосекторне raw-читання SD картки в SPI-режимі (CMD18 READ_MULTIPLE_BLOCK).
//
// НАВІЩО: публічний SD.readRAW() читає РІВНО один сектор за виклик, тобто на
// кожні 512 байт іде окрема команда CMD17 з очікуванням токена даних. Замір
// на esp32-c6 (команда "sdbench"): 246 KiB/s на SD_FREQ=4 МГц і 391 KiB/s на
// 40 МГц - десятикратне підняття частоти дало +59%. Отже швидкість уперлась
// не в шину, а в накладні витрати на команду, і єдиний спосіб її підняти -
// читати пачку секторів ОДНІЄЮ командою (виміряно: 1.35 MiB/s при 64
// секторах, утричі швидше).
//
// Драйвер arduino-esp32 це вміє (sd_diskio.cpp: ff_sd_read() при count > 1
// викликає sdReadSectors() -> CMD18), але клас SDFS цей шлях у публічний API
// не виводить - readRAW() жорстко передає count = 1.
//
// ЦІНА РІШЕННЯ: ff_sd_read() - внутрішній символ фреймворку, у sd_diskio.h
// він не оголошений, тому прототип продубльований у .cpp і може розійтися з
// фреймворком при оновленні core. Компенсація - у begin(): номер драйвера
// (pdrv) НЕ вгадується, а перевіряється через публічні
// sdcard_num_sectors()/sdcard_sector_size(). Це не перестороженість:
// ff_sd_read() розіменовує s_cards[pdrv] без перевірки на NULL, тому
// помилковий pdrv - це не невдале читання, а миттєвий reset плати.
class SdSpiBulkReader : public SdBulkReader {
public:
  // Шукає номер драйвера змонтованої картки і перевіряє його.
  // expectedSectors - результат SD.numSectors().
  bool begin(size_t expectedSectors);

  bool isReady() const override { return _isReady; }
  bool readSectors(uint32_t lba, uint32_t count, uint8_t *out) override;

  uint8_t pdrv() const { return _pdrv; }

private:
  uint8_t _pdrv = 0xFF;  // 0xFF - "не знайдено", те саме значення-маркер, що й у SDFS
  bool _isReady = false;
};

#endif  // ESP32
