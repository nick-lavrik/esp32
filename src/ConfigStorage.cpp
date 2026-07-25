#include "ConfigStorage.h"

ConfigStorage::ConfigStorage() {}

bool ConfigStorage::begin(const char* namespaceName, const char* partitionLabel) {
    namespaceName_ = namespaceName;
    partitionLabel_ = partitionLabel;
    return prefs_.begin(namespaceName, false, partitionLabel);
}

void ConfigStorage::end() {
    prefs_.end();
}

void ConfigStorage::setInt(const char* key, int32_t value) {
    prefs_.putInt(key, value);
}

int32_t ConfigStorage::getInt(const char* key, int32_t defaultValue) {
    return prefs_.getInt(key, defaultValue);
}

void ConfigStorage::setFloat(const char* key, float value) {
    prefs_.putFloat(key, value);
}

float ConfigStorage::getFloat(const char* key, float defaultValue) {
    return prefs_.getFloat(key, defaultValue);
}

void ConfigStorage::setString(const char* key, const String& value) {
    prefs_.putString(key, value);
}

String ConfigStorage::getString(const char* key, const String& defaultValue) {
    return prefs_.getString(key, defaultValue);
}

void ConfigStorage::setFloatArray(const char* key, const float* arr, size_t count) {
    prefs_.putBytes(key, arr, count * sizeof(float));
}

size_t ConfigStorage::getFloatArray(const char* key, float* outArr, size_t maxCount) {
    size_t storedBytes = prefs_.getBytesLength(key);
    size_t maxBytes = maxCount * sizeof(float);
    size_t bytesToRead = storedBytes < maxBytes ? storedBytes : maxBytes;
    prefs_.getBytes(key, outArr, bytesToRead);
    return bytesToRead / sizeof(float);
}

void ConfigStorage::clearAll() {
    prefs_.clear();
}

std::vector<ConfigStorage::Entry> ConfigStorage::listEntries() {
    std::vector<Entry> result;

    nvs_iterator_t it = nullptr;
    esp_err_t res = nvs_entry_find(partitionLabel_.c_str(), namespaceName_.c_str(), NVS_TYPE_ANY, &it);

    while (res == ESP_OK) {
        nvs_entry_info_t info;
        nvs_entry_info(it, &info);

        Entry entry;
        entry.key = info.key;
        entry.type = info.type;
        entry.typeName = typeToString(info.type);
        result.push_back(entry);

        res = nvs_entry_next(&it);
    }
    nvs_release_iterator(it);

    return result;
}

nvs_type_t ConfigStorage::getType(const char* key) {
    nvs_iterator_t it = nullptr;
    esp_err_t res = nvs_entry_find(partitionLabel_.c_str(), namespaceName_.c_str(), NVS_TYPE_ANY, &it);

    nvs_type_t found = NVS_TYPE_ANY;
    while (res == ESP_OK) {
        nvs_entry_info_t info;
        nvs_entry_info(it, &info);
        if (strcmp(info.key, key) == 0) {
            found = info.type;
            break;
        }
        res = nvs_entry_next(&it);
    }
    nvs_release_iterator(it);
    return found;
}

String ConfigStorage::typeToString(nvs_type_t type) {
    switch (type) {
        case NVS_TYPE_U8:   return "u8";
        case NVS_TYPE_I8:   return "i8";
        case NVS_TYPE_U16:  return "u16";
        case NVS_TYPE_I16:  return "i16";
        case NVS_TYPE_U32:  return "u32";
        case NVS_TYPE_I32:  return "i32";
        case NVS_TYPE_U64:  return "u64";
        case NVS_TYPE_I64:  return "i64";
        case NVS_TYPE_STR:  return "string";
        case NVS_TYPE_BLOB: return "blob";
        default:            return "unknown";
    }
}

bool ConfigStorage::peekStructHeader(const char* key, BlobHeader& outHeader) {
    size_t storedLen = prefs_.getBytesLength(key);
    if (storedLen < sizeof(BlobHeader)) return false;
    prefs_.getBytes(key, &outHeader, sizeof(BlobHeader));
    return true;
}
