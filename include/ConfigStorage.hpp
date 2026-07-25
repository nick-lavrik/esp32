#pragma once
#include <Preferences.h>
#include <Arduino.h>
#include <vector>
#include <nvs.h>
#include <nvs_flash.h>

// Обгортка над ESP32 NVS (Non-Volatile Storage) для зберігання конфігурації
// між перезавантаженнями / вимкненнями пристрою.
//
// Підтримує: int, float, String, масив float, довільні структури (з версією),
// а також перелік всіх ключів у namespace та визначення їх типу.
class ConfigStorage {
public:
    // ---- допоміжні типи (nested, щоб не плодити окремі файли під невеликі POD) ----
    struct Entry {
        String key;
        nvs_type_t type;
        String typeName;
    };

    enum class StructReadResult {
        OK,
        NOT_FOUND,
        MAGIC_MISMATCH,
        VERSION_MISMATCH,
        SIZE_MISMATCH
    };

    // Заголовок, що зберігається перед байтами довільної структури.
    // Дозволяє при читанні перевірити, чи сумісна збережена структура
    // з тією, яку очікує поточна версія прошивки.
    struct BlobHeader {
        uint32_t magic;
        uint16_t version;
        uint16_t size;
    };

    ConfigStorage();

    // namespaceName обмежений 15 символами (обмеження NVS)
    bool begin(const char* namespaceName = "config", const char* partitionLabel = "nvs");
    void end();

    // ---- bool ----
    void setBool(const char* key, const bool value);
    const bool getBool(const char* key, const bool defaultValue = 0);

    // ---- int ----
    void setInt(const char* key, int32_t value);
    int32_t getInt(const char* key, int32_t defaultValue = 0);

    // ---- float ----
    void setFloat(const char* key, float value);
    float getFloat(const char* key, float defaultValue = 0.0f);

    // ---- String ----
    void setString(const char* key, const String& value);
    String getString(const char* key, const String& defaultValue = "");

    // ---- масив float ----
    void setFloatArray(const char* key, const float* arr, size_t count);
    size_t getFloatArray(const char* key, float* outArr, size_t maxCount);

    // видаляє ВСІ ключі в поточному namespace (обережно!)
    void clearAll();

    // ---- перелік записів у поточному namespace ----
    std::vector<Entry> listEntries();

    // тип конкретного ключа (NVS_TYPE_ANY якщо не знайдено)
    nvs_type_t getType(const char* key);
    static String typeToString(nvs_type_t type);

    // ---- довільні структури з перевіркою версії/розміру/magic ----
    template<typename T>
    bool setStruct(const char* key, const T& value, uint16_t version, uint32_t magic) {
        BlobHeader header{magic, version, static_cast<uint16_t>(sizeof(T))};
        std::vector<uint8_t> buffer(sizeof(BlobHeader) + sizeof(T));
        memcpy(buffer.data(), &header, sizeof(BlobHeader));
        memcpy(buffer.data() + sizeof(BlobHeader), &value, sizeof(T));
        size_t written = prefs_.putBytes(key, buffer.data(), buffer.size());
        return written == buffer.size();
    }

    template<typename T>
    StructReadResult getStruct(const char* key, T& outValue, uint16_t expectedVersion, uint32_t expectedMagic) {
        size_t storedLen = prefs_.getBytesLength(key);
        if (storedLen == 0) return StructReadResult::NOT_FOUND;
        if (storedLen < sizeof(BlobHeader)) return StructReadResult::SIZE_MISMATCH;

        std::vector<uint8_t> buffer(storedLen);
        prefs_.getBytes(key, buffer.data(), storedLen);

        BlobHeader header;
        memcpy(&header, buffer.data(), sizeof(BlobHeader));

        if (header.magic != expectedMagic) return StructReadResult::MAGIC_MISMATCH;
        if (header.version != expectedVersion) return StructReadResult::VERSION_MISMATCH;
        if (header.size != sizeof(T)) return StructReadResult::SIZE_MISMATCH;

        memcpy(&outValue, buffer.data() + sizeof(BlobHeader), sizeof(T));
        return StructReadResult::OK;
    }

    // читає лише заголовок блоба (magic/version/size) без розпакування даних —
    // корисно, щоб дізнатись версію ПЕРЕД тим, як обирати цільову структуру
    bool peekStructHeader(const char* key, BlobHeader& outHeader);

private:
    Preferences prefs_;
    String namespaceName_;
    String partitionLabel_;
};
