#include "EspPartitionInspector.hpp"

#include <esp_partition.h>
#include <cstdio>
#include <cstring>

namespace {

// 0xE9 — magic byte заголовка валідного ESP app image (esp_app_format.h,
// ESP_IMAGE_HEADER_MAGIC). Використовуємо літерал, щоб не тягнути зайвий
// заголовок лише заради однієї константи.
constexpr uint8_t kEspAppImageMagic = 0xE9;

namespace PartitionState {
    inline constexpr const char* Unknown = "Unknown";
    inline constexpr const char* ReadError = "read-error";
    inline constexpr const char* ErasedEmpty = "erased/empty";
    inline constexpr const char* ValidAppImage = "valid-app-image";
    inline constexpr const char* InvalidAppImage = "invalid-app-image";
    inline constexpr const char* FatSignatureOk = "fat-signature-ok";
    inline constexpr const char* FatSignatureMissing = "fat-signature-missing";
    inline constexpr const char* NvsDataPresent = "nvs-data-present";
    inline constexpr const char* DataPresent = "data-present";
    inline constexpr const char* Present = "present";
}

bool isErased(const uint8_t *buffer, size_t len) {
    for (size_t i = 0; i < len; ++i) {
        if (buffer[i] != 0xFF) {
            return false;
        }
    }
    return true;
}

} // namespace

std::string EspPartitionInspector::partitionTypeToString(uint8_t type) {
    switch (type) {
        case ESP_PARTITION_TYPE_APP:
            return "app";
        case ESP_PARTITION_TYPE_DATA:
            return "data";
        default: {
            char buffer[8];
            snprintf(buffer, sizeof(buffer), "0x%02X", type);
            return std::string(buffer);
        }
    }
}

std::string EspPartitionInspector::partitionSubtypeToString(uint8_t type, uint8_t subtype) {
    if (type == ESP_PARTITION_TYPE_APP) {
        if (subtype == ESP_PARTITION_SUBTYPE_APP_FACTORY) {
            return "factory";
        }
        if (subtype == ESP_PARTITION_SUBTYPE_APP_TEST) {
            return "test";
        }
        if (subtype >= ESP_PARTITION_SUBTYPE_APP_OTA_MIN && subtype < ESP_PARTITION_SUBTYPE_APP_OTA_MAX) {
            char buffer[16];
            snprintf(buffer, sizeof(buffer), "ota_%d", subtype - ESP_PARTITION_SUBTYPE_APP_OTA_MIN);
            return std::string(buffer);
        }
    } else if (type == ESP_PARTITION_TYPE_DATA) {
        switch (subtype) {
            case ESP_PARTITION_SUBTYPE_DATA_OTA:
                return "ota_data";
            case ESP_PARTITION_SUBTYPE_DATA_PHY:
                return "phy";
            case ESP_PARTITION_SUBTYPE_DATA_NVS:
                return "nvs";
            case ESP_PARTITION_SUBTYPE_DATA_COREDUMP:
                return "coredump";
            case ESP_PARTITION_SUBTYPE_DATA_NVS_KEYS:
                return "nvs_keys";
            case ESP_PARTITION_SUBTYPE_DATA_EFUSE_EM:
                return "efuse";
            case ESP_PARTITION_SUBTYPE_DATA_SPIFFS:
                return "spiffs";
            case 0x83: // офіційної ESP_PARTITION_SUBTYPE_DATA_LITTLEFS константи немає,
                       // 0x83 — де-факто прийняте значення (Arduino LittleFS, PlatformIO)
                return "littlefs";
            case ESP_PARTITION_SUBTYPE_DATA_FAT:
                return "fat";
            default:
                break;
        }
    }

    char buffer[8];
    snprintf(buffer, sizeof(buffer), "0x%02X", subtype);
    return std::string(buffer);
}

std::string EspPartitionInspector::detectState(const esp_partition_t *partition) {
    if (partition == nullptr) {
        return PartitionState::Unknown;
    }

    uint8_t header[32] = {};
    esp_err_t err = esp_partition_read(partition, 0, header, sizeof(header));
    if (err != ESP_OK) {
        return PartitionState::ReadError;
    }

    if (isErased(header, sizeof(header))) {
        return PartitionState::ErasedEmpty;
    }

    if (partition->type == ESP_PARTITION_TYPE_APP) {
        return (header[0] == kEspAppImageMagic)
            ? PartitionState::ValidAppImage
            : PartitionState::InvalidAppImage;
    }

    if (partition->type == ESP_PARTITION_TYPE_DATA) {
        if (partition->subtype == ESP_PARTITION_SUBTYPE_DATA_FAT) {
            uint8_t bootSector[512] = {};
            if (esp_partition_read(partition, 0, bootSector, sizeof(bootSector)) == ESP_OK) {
                bool signatureOk = (bootSector[510] == 0x55 && bootSector[511] == 0xAA);
                return signatureOk ? PartitionState::FatSignatureOk : PartitionState::FatSignatureMissing;
            }
            return PartitionState::ReadError;
        }
        if (partition->subtype == ESP_PARTITION_SUBTYPE_DATA_NVS) {
            // Формат сторінок NVS внутрішній для компонента nvs_flash;
            // тут лише підтверджуємо, що розділ не порожній.
            return PartitionState::NvsDataPresent;
        }
        return PartitionState::DataPresent;
    }

    return PartitionState::Present;
}

bool EspPartitionInspector::readSha256(const esp_partition_t *partition, uint8_t *out32) {
    if (partition == nullptr) {
        return false;
    }
    return esp_partition_get_sha256(partition, out32) == ESP_OK;
}

std::string EspPartitionInspector::sha256ToHex(const uint8_t *sha256) {
    static const char *kHex = "0123456789abcdef";
    std::string result;
    result.reserve(64);
    for (int i = 0; i < 32; ++i) {
        result.push_back(kHex[sha256[i] >> 4]);
        result.push_back(kHex[sha256[i] & 0x0F]);
    }
    return result;
}

std::vector<EspPartitionInfo> EspPartitionInspector::collectAll(bool computeSha256) {
    std::vector<EspPartitionInfo> result;

    esp_partition_iterator_t it = esp_partition_find(ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY, nullptr);

    for (; it != nullptr; it = esp_partition_next(it)) {
        const esp_partition_t *partition = esp_partition_get(it);
        if (partition == nullptr) {
            continue;
        }

        EspPartitionInfo info;
        info.label = partition->label;
        info.typeName = partitionTypeToString(partition->type);
        info.subtypeName = partitionSubtypeToString(partition->type, partition->subtype);
        info.offset = partition->address;
        info.size = partition->size;
        info.encrypted = partition->encrypted;
        info.readOnly = partition->readonly;
        info.state = detectState(partition);

        if (computeSha256) {
            info.sha256Valid = readSha256(partition, info.sha256);
        }

        result.push_back(info);
    }
    // esp_partition_next() сам звільняє ітератор і повертає nullptr в кінці,
    // тому виклик release тут потрібен лише для безпеки (no-op при nullptr).
    esp_partition_iterator_release(it);

    return result;
}

void EspPartitionInspector::printOne(const EspPartitionInfo &info, Print &out) {
    out.printf("%-12s %-6s %-10s 0x%08X 0x%08X %-5s %-9s %s\n",
               info.label.c_str(),
               info.typeName.c_str(),
               info.subtypeName.c_str(),
               static_cast<unsigned int>(info.offset),
               static_cast<unsigned int>(info.size),
               info.encrypted ? "yes" : "no",
               info.readOnly ? "yes" : "no",
               info.state.c_str());

    if (info.sha256Valid) {
        out.printf("             sha256: %s\n", sha256ToHex(info.sha256).c_str());
    }
}

void EspPartitionInspector::printAll(Print &out, bool computeSha256) {
    char line[] = "-----------------------------------";
    auto partitions = collectAll(computeSha256);

    out.println(F("=== Flash Partition Table ==="));
    out.printf("%-12s %-6s %-10s %-10s %-10s %-5s %-9s %s\n",
               "label", "type", "subtype", "offset", "size", "encr", "readonly", "state");
    out.printf("%.12s %.6s %.10s %.10s %.10s %.5s %.9s %s\n",
               line, line, line, line, line, line, line, line);

    for (const auto &info : partitions) {
        printOne(info, out);
    }

    out.printf("%.12s-%.6s-%.10s-%.10s-%.10s-%.5s-%.9s-%s\n",
               line, line, line, line, line, line, line, line);

    out.printf("Total: %u partitions\n", static_cast<unsigned int>(partitions.size()));
}
