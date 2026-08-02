#pragma once
#include <Arduino.h>
#include <vector>

#if defined(ESP32)
    #include <Preferences.h>
    #include <nvs.h>
    #include <nvs_flash.h>
#elif defined(ESP8266)
    #include <LittleFS.h>

    // ESP8266 не має заголовку nvs.h — визначаємо власний shim з тими самими
    // іменами енумераторів, щоб публічний API (Entry::type, getType(),
    // typeToString()) залишався ідентичним для обох платформ.
    // Реальних "float"/"bool" типів у NVS немає (Preferences зберігає їх як blob),
    // тому на ESP8266 такі файли теж мапляться в NVS_TYPE_BLOB.
    typedef enum {
        NVS_TYPE_U8   = 0x01,
        NVS_TYPE_I8   = 0x11,
        NVS_TYPE_U16  = 0x02,
        NVS_TYPE_I16  = 0x12,
        NVS_TYPE_U32  = 0x04,
        NVS_TYPE_I32  = 0x14,
        NVS_TYPE_U64  = 0x08,
        NVS_TYPE_I64  = 0x18,
        NVS_TYPE_STR  = 0x21,
        NVS_TYPE_BLOB = 0x42,
        NVS_TYPE_ANY  = 0xff
    } nvs_type_t;
#endif

// Обгортка над персистентним key-value сховищем конфігурації.
// ESP32   -> NVS (Preferences.h), namespace = NVS namespace.
// ESP8266 -> LittleFS, namespace = каталог, кожен параметр = окремий файл
//            "/.nvs/<namespace>/<key>.<type>", type in {str,i32,u32,f32,bool,bin}.
//
// Публічний API однаковий для обох платформ.
class ConfigStorage {
public:
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
    struct BlobHeader {
        uint32_t magic;
        uint16_t version;
        uint16_t size;
    };

    // Обмеження ключа: ASCII, довжина 1..15 (успадковано від NVS, зберігається
    // однаковим для обох платформ заради єдиної поведінки).
    static constexpr size_t MAX_KEY_LENGTH = 15;

    static bool isKeyValid(const char* key);

    ConfigStorage();

    bool begin(const char* namespaceName = "config", const char* partitionLabel = "nvs");
    void end();

    // ---- int ----
    void setInt(const char* key, int32_t value);
    int32_t getInt(const char* key, int32_t defaultValue = 0);

    // ---- float ----
    void setFloat(const char* key, float value);
    float getFloat(const char* key, float defaultValue = 0.0f);

    // ---- String ----
    void setString(const char* key, const String& value);
    String getString(const char* key, const String& defaultValue = "");

    // ---- bool ----
    void setBool(const char* key, bool value);
    bool getBool(const char* key, bool defaultValue = false);

    // ---- масив float ----
    void setFloatArray(const char* key, const float* arr, size_t count);
    size_t getFloatArray(const char* key, float* outArr, size_t maxCount);

    // ---- масив String ----
    // Формат: [uint16_t count][uint16_t len1][bytes1][uint16_t len2][bytes2]...
    void setStringArray(const char* key, const std::vector<String>& arr);
    size_t getStringArray(const char* key, std::vector<String>& outArr);

    // видаляє ВСІ ключі в поточному namespace
    void clearAll();

    // перелік записів у поточному namespace
    std::vector<Entry> listEntries();

    // тип конкретного ключа (NVS_TYPE_ANY якщо не знайдено)
    nvs_type_t getType(const char* key);
    static String typeToString(nvs_type_t type);

    // ---- довільні структури з перевіркою версії/розміру/magic ----
    template<typename T>
    bool setStruct(const char* key, const T& value, uint16_t version, uint32_t magic) {
        if (!isKeyValid(key)) {
            warnInvalidKey(key, "setStruct");
            return false;
        }
        BlobHeader header{magic, version, static_cast<uint16_t>(sizeof(T))};
        std::vector<uint8_t> buffer(sizeof(BlobHeader) + sizeof(T));
        memcpy(buffer.data(), &header, sizeof(BlobHeader));
        memcpy(buffer.data() + sizeof(BlobHeader), &value, sizeof(T));
        size_t written = writeBlob(key, buffer.data(), buffer.size());
        return written == buffer.size();
    }

    template<typename T>
    StructReadResult getStruct(const char* key, T& outValue, uint16_t expectedVersion, uint32_t expectedMagic) {
        if (!isKeyValid(key)) {
            warnInvalidKey(key, "getStruct");
            return StructReadResult::SIZE_MISMATCH;
        }
        size_t storedLen = blobLength(key);
        if (storedLen == 0) return StructReadResult::NOT_FOUND;
        if (storedLen < sizeof(BlobHeader)) return StructReadResult::SIZE_MISMATCH;

        std::vector<uint8_t> buffer(storedLen);
        readBlob(key, buffer.data(), storedLen);

        BlobHeader header;
        memcpy(&header, buffer.data(), sizeof(BlobHeader));

        if (header.magic != expectedMagic) return StructReadResult::MAGIC_MISMATCH;
        if (header.version != expectedVersion) return StructReadResult::VERSION_MISMATCH;
        if (header.size != sizeof(T)) return StructReadResult::SIZE_MISMATCH;

        memcpy(&outValue, buffer.data() + sizeof(BlobHeader), sizeof(T));
        return StructReadResult::OK;
    }

    // читає лише заголовок блоба без розпакування даних
    bool peekStructHeader(const char* key, BlobHeader& outHeader);

private:
#if defined(ESP32)
    Preferences prefs_;
#endif
    String namespaceName_;
    String partitionLabel_;

    static void warnInvalidKey(const char* key, const char* methodName);

    // --- уніфіковані blob-примітиви (для setStruct/getStruct/масивів) ---
    // ESP32:   зберігається одним NVS-ключем (putBytes/getBytes).
    // ESP8266: зберігається у файлі "<key>.bin" в каталозі namespace.
    size_t writeBlob(const char* key, const void* data, size_t len);
    size_t readBlob(const char* key, void* outData, size_t maxLen);
    size_t blobLength(const char* key);

#if defined(ESP8266)
    // допоміжні методи файлової реалізації
    String pathFor(const char* key, const char* ext) const;
    String namespaceDir() const;
    bool ensureNamespaceDir() const;
    bool writeFile(const String& path, const void* data, size_t len);
    size_t readFile(const String& path, void* outData, size_t maxLen);
#endif
};
