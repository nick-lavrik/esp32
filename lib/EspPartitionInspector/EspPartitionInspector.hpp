#pragma once

#include <vector>
#include <string>
#include <cstdint>
#include <Print.h>
#include <esp_partition.h>

#include "EspPartitionInfo.hpp"

// esp_partition_t в ESP-IDF оголошений як `typedef struct { ... } esp_partition_t;`
// (анонімна структура), тому forward-declare на кшталт `struct esp_partition_t;`
// неможливий — це створює інший, конфліктуючий тип. Доводиться інклюжити
// esp_partition.h напряму в заголовку.

// Читає таблицю розділів (partition table) з флеш-пам'яті ESP32 у рантаймі
// та надає базову діагностику кожного розділу (тип, підтип, розмір,
// прапорець шифрування, евристичний стан, опційно SHA-256).
//
// Працює однаково на класичному ESP32 та ESP32-S3 (esp_partition_* API
// незалежний від конкретного чіпа), тому окремих гілок #if defined(BOARD_XXX)
// не потрібно.
class EspPartitionInspector {
public:
    // Зчитує всі розділи з таблиці. Якщо computeSha256 == true, додатково
    // рахує SHA-256 кожного розділу (esp_partition_get_sha256) — це читає
    // весь розділ через SPI флеш і може зайняти помітний час для великих
    // app-розділів (сотні мс і більше), тому за замовчуванням вимкнено.
    static std::vector<EspPartitionInfo> collectAll(bool computeSha256 = false);

    // Друкує таблицю розділів у вигляді, зручному для serial-виводу.
    static void printAll(Print &out, bool computeSha256 = false);

    // Друкує один вже зібраний EspPartitionInfo (для кастомного форматування).
    static void printOne(const EspPartitionInfo &info, Print &out);

private:
    static std::string partitionTypeToString(uint8_t type);
    static std::string partitionSubtypeToString(uint8_t type, uint8_t subtype);
    static std::string detectState(const esp_partition_t *partition);
    static bool readSha256(const esp_partition_t *partition, uint8_t *out32);
    static std::string sha256ToHex(const uint8_t *sha256);
};
