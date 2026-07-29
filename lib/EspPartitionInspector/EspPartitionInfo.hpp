#pragma once

#include <cstdint>
#include <string>

// Дані про один запис у таблиці розділів (partition table) флеш-пам'яті.
// Заповнюється класом EspPartitionInspector.
struct EspPartitionInfo {
    std::string label;        // мітка з partitions.csv, напр. "app0", "nvs", "spiffs"
    std::string typeName;     // "app" / "data" / "0xNN"
    std::string subtypeName;  // "factory" / "ota_0" / "nvs" / "spiffs" / "littlefs" / "fat" тощо
    uint32_t offset = 0;      // адреса початку в флеш-пам'яті
    uint32_t size = 0;        // розмір у байтах
    bool encrypted = false;   // прапорець шифрування розділу (з esp_partition_t::encrypted)
    bool readOnly = false;    // прапорець "тільки читання" (з esp_partition_t::readonly)
    std::string state;        // евристичний стан: "erased/empty", "valid-app-image", "fat-signature-ok" тощо

    bool sha256Valid = false; // true, якщо sha256 нижче було успішно обчислено
    uint8_t sha256[32] = {};  // контрольна сума розділу (esp_partition_get_sha256), опційно
};
