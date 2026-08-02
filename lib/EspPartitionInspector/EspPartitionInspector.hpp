#pragma once

#include <vector>
#include <string>
#include <cstdint>
#include <Print.h>

#if !defined(ESP8266)
// esp_partition_t в ESP-IDF оголошений як `typedef struct { ... } esp_partition_t;`
// (анонімна структура), тому forward-declare на кшталт `struct esp_partition_t;`
// неможливий — це створює інший, конфліктуючий тип. Доводиться інклюжити
// esp_partition.h напряму в заголовку.
#include <esp_partition.h>
#endif

#include "EspPartitionInfo.hpp"

// Читає розкладку флеш-пам'яті ESP32/ESP8266 у рантаймі та надає базову
// діагностику кожного регіону (тип, підтип, розмір, стан, опційно SHA-256).
//
// ESP32/ESP32-S3: реальна таблиця розділів (esp_partition_* API, однакове
// на класичному ESP32 та ESP32-S3, окремих #if defined(BOARD_XXX) не треба).
//
// ESP8266: таблиці розділів не існує в принципі (Arduino core / non-OS SDK
// не мають цього поняття) — розкладка фіксована лінкер-скриптом
// (eagle.flash.*.ld). Тому для ESP8266 EspPartitionInfo заповнюється
// синтетичними записами (sketch / ota-free / filesystem / eeprom /
// flash-total), зібраними з лінкер-символів та ESP.h API. sha256 для
// ESP8266 не рахується (computeSha256 ігнорується).
class EspPartitionInspector {
public:
    static std::vector<EspPartitionInfo> collectAll(bool computeSha256 = false);

    // Друкує таблицю розділів (або, на ESP8266, синтетичний список регіонів)
    // у вигляді, зручному для serial-виводу.
    static void printAll(Print &out, bool computeSha256 = false);

    // Друкує один вже зібраний EspPartitionInfo (для кастомного форматування).
    static void printOne(const EspPartitionInfo &info, Print &out);

private:
#if defined(ESP8266)
    static std::vector<EspPartitionInfo> collectAllEsp8266();
#else
    static std::vector<EspPartitionInfo> collectAllEsp32(bool computeSha256);
    static std::string partitionTypeToString(uint8_t type);
    static std::string partitionSubtypeToString(uint8_t type, uint8_t subtype);
    static std::string detectState(const esp_partition_t *partition);
    static bool readSha256(const esp_partition_t *partition, uint8_t *out32);
#endif
    static std::string sha256ToHex(const uint8_t *sha256);
};
