#include "EspPartitionInspector.hpp"

#include <cstdio>
#include <cstring>

// ============================================================================
// ESP32 / ESP32-S3: реальна таблиця розділів через esp_partition_* (ESP-IDF)
// ============================================================================
#if !defined(ESP8266)

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

std::vector<EspPartitionInfo> EspPartitionInspector::collectAllEsp32(bool computeSha256) {
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

#else
// ============================================================================
// ESP8266: таблиці розділів немає. Розкладка фіксована лінкер-скриптом
// (eagle.flash.*.ld). Тут будуємо синтетичний список регіонів з лінкер-
// символів та Arduino ESP8266 core API (Esp.h).
// ============================================================================

#include <Arduino.h>
#include <Esp.h>
#include <ILogger.hpp>

extern "C" {
    // _FS_start/_FS_end/_FS_page/_FS_block — актуальні назви символів у
    // сучасних версіях ESP8266 Arduino core (LittleFS/SPIFFS регіон).
    // У старіших core ці ж символи називались _SPIFFS_start/_SPIFFS_end.
    extern uint32_t _FS_start;
    extern uint32_t _FS_end;
    extern uint32_t _EEPROM_start;
}

namespace {
    // Базова адреса, з якої лінкер-символи (вказівники в адресному просторі
    // 0x40200000+) мапляться назад у фізичний offset у flash. Те саме
    // обчислення використовує офіційний приклад FSBrowser з ESP8266 core.
    constexpr uint32_t kFlashMapBase = 0x40200000;

    // EEPROM-емуляція на ESP8266 (бібліотека EEPROM.h) завжди резервує
    // рівно один сектор флеш — 4096 байт, незалежно від EEPROM.begin(size).
    constexpr uint32_t kEepromSectorSize = 4096;
}

std::vector<EspPartitionInfo> EspPartitionInspector::collectAllEsp8266() {
    std::vector<EspPartitionInfo> result;

    const uint32_t flashChipSize = ESP.getFlashChipSize();
    const uint32_t flashChipRealSize = ESP.getFlashChipRealSize();
    const uint32_t sketchSize = ESP.getSketchSize();
    const uint32_t freeSketchSpace = ESP.getFreeSketchSpace();
    const uint32_t fsStart = reinterpret_cast<uint32_t>(&_FS_start) - kFlashMapBase;
    const uint32_t fsEnd = reinterpret_cast<uint32_t>(&_FS_end) - kFlashMapBase;
    const uint32_t eepromStart = reinterpret_cast<uint32_t>(&_EEPROM_start) - kFlashMapBase;

    EspPartitionInfo sketch;
    sketch.label = "sketch";
    sketch.typeName = "app";
    sketch.subtypeName = "current";
    sketch.offset = 0x0;
    sketch.size = sketchSize;
    sketch.state = "valid-app-image"; // якщо це виконується - образ вже валідний
    result.push_back(sketch);

    EspPartitionInfo otaFree;
    otaFree.label = "ota-free-space";
    otaFree.typeName = "app";
    // ESP8266 не має окремого OTA-розділу як такого: eboot просто пише новий
    // образ у вільний простір після поточного скетчу (ESP.getFreeSketchSpace()),
    // це не фізична партиція, а обчислене вільне місце.
    otaFree.subtypeName = "computed-free";
    otaFree.offset = sketchSize;
    otaFree.size = freeSketchSpace;
    otaFree.state = "free-space";
    result.push_back(otaFree);

    EspPartitionInfo filesystem;
    filesystem.label = "filesystem";
    filesystem.typeName = "data";
    filesystem.subtypeName = "littlefs/spiffs"; // фактичний тип визначає board_build.filesystem
    filesystem.offset = fsStart;
    filesystem.size = fsEnd - fsStart;
    filesystem.state = "present";
    result.push_back(filesystem);

    EspPartitionInfo eeprom;
    eeprom.label = "eeprom";
    eeprom.typeName = "data";
    eeprom.subtypeName = "eeprom";
    eeprom.offset = eepromStart;
    eeprom.size = kEepromSectorSize;
    eeprom.state = "present";
    result.push_back(eeprom);

    EspPartitionInfo flashTotal;
    flashTotal.label = "flash-chip";
    flashTotal.typeName = "info";
    flashTotal.subtypeName = (flashChipSize == flashChipRealSize) ? "size-matches-real" : "size-mismatch";
    flashTotal.offset = 0;
    flashTotal.size = flashChipSize;
    flashTotal.state = "n/a";
    result.push_back(flashTotal);

    return result;
}

#endif // !defined(ESP8266)

// ============================================================================
// Спільна частина для обох платформ
// ============================================================================

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
#if defined(ESP8266)
    (void)computeSha256; // SHA-256 для синтетичних регіонів ESP8266 не рахується
    return collectAllEsp8266();
#else
    return collectAllEsp32(computeSha256);
#endif
}

void EspPartitionInspector::printOne(const EspPartitionInfo &info, Print& out) {
    out.printf("%-14s %-7s %-17s 0x%08X 0x%08X %-4s %-8s %s\n",
               info.label.c_str(),
               info.typeName.c_str(),
               info.subtypeName.c_str(),
               static_cast<unsigned int>(info.offset),
               static_cast<unsigned int>(info.size),
               info.encrypted ? "yes" : "no",
               info.readOnly ? "yes" : "no",
               info.state.c_str());

    if (info.sha256Valid) {
        out.printf("               sha256: %s\n", sha256ToHex(info.sha256).c_str());
    }
}

void EspPartitionInspector::printAll(Print &out, bool computeSha256) {
    char line[] = "-----------------------------------";
    auto partitions = collectAll(computeSha256);

#if defined(ESP8266)
    out.println("=== ESP8266 Flash Layout (synthetic, no partition table) ===");
#else
    out.println("=== Flash Partition Table ===");
#endif
    out.printf("%-14s %-7s %-17s %-10s %-10s %-4s %-8s %s\n",
               "label", "type", "subtype", "offset", "size", "encr", "readonly", "state");
    out.printf("%.14s %.7s %.17s %.10s %.10s %.4s %.8s %s\n",
               line, line, line, line, line, line, line, line);

    for (const auto &info : partitions) {
        printOne(info, out);
    }

    out.printf("%.14s-%.7s-%.17s-%.10s-%.10s-%.4s-%.8s-%s\n",
               line, line, line, line, line, line, line, line);

    out.printf("Total: %u entries\n", static_cast<unsigned int>(partitions.size()));
}
