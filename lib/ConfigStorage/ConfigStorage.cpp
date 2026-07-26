#include "ConfigStorage.hpp"

ConfigStorage::ConfigStorage() {}

bool ConfigStorage::isKeyValid(const char* key) {
    if (key == nullptr) return false;
    size_t len = strlen(key);
    return len > 0 && len <= MAX_KEY_LENGTH;
}

void ConfigStorage::warnInvalidKey(const char* key, const char* methodName) {
    Serial.printf("[ConfigStorage::%s] Некоректний ключ \"%s\" (довжина %d, максимум %d символів)\n",
                  methodName,
                  key ? key : "(nullptr)",
                  key ? strlen(key) : 0,
                  static_cast<int>(MAX_KEY_LENGTH));
}

bool ConfigStorage::begin(const char* namespaceName, const char* partitionLabel) {
    if (!isKeyValid(namespaceName)) {
        warnInvalidKey(namespaceName, "begin (namespace)");
        return false;
    }
    namespaceName_ = namespaceName;
    partitionLabel_ = partitionLabel;
    return prefs_.begin(namespaceName, false, partitionLabel);
}

void ConfigStorage::end() {
    prefs_.end();
}

void ConfigStorage::setInt(const char* key, int32_t value) {
    if (!isKeyValid(key)) { warnInvalidKey(key, "setInt"); return; }
    prefs_.putInt(key, value);
}

int32_t ConfigStorage::getInt(const char* key, int32_t defaultValue) {
    if (!isKeyValid(key)) { warnInvalidKey(key, "getInt"); return defaultValue; }
    return prefs_.getInt(key, defaultValue);
}

void ConfigStorage::setFloat(const char* key, float value) {
    if (!isKeyValid(key)) { warnInvalidKey(key, "setFloat"); return; }
    prefs_.putFloat(key, value);
}

float ConfigStorage::getFloat(const char* key, float defaultValue) {
    if (!isKeyValid(key)) { warnInvalidKey(key, "getFloat"); return defaultValue; }
    return prefs_.getFloat(key, defaultValue);
}

void ConfigStorage::setString(const char* key, const String& value) {
    if (!isKeyValid(key)) { warnInvalidKey(key, "setString"); return; }
    prefs_.putString(key, value);
}

String ConfigStorage::getString(const char* key, const String& defaultValue) {
    if (!isKeyValid(key)) { warnInvalidKey(key, "getString"); return defaultValue; }
    return prefs_.getString(key, defaultValue);
}

void ConfigStorage::setBool(const char* key, bool value) {
    if (!isKeyValid(key)) { warnInvalidKey(key, "setBool"); return; }
    prefs_.putBool(key, value);
}

bool ConfigStorage::getBool(const char* key, bool defaultValue) {
    if (!isKeyValid(key)) { warnInvalidKey(key, "getBool"); return defaultValue; }
    return prefs_.getBool(key, defaultValue);
}

void ConfigStorage::setStringArray(const char* key, const std::vector<String>& arr) {
    if (!isKeyValid(key)) { warnInvalidKey(key, "setStringArray"); return; }

    // серіалізація: [count][len1][bytes1][len2][bytes2]...
    std::vector<uint8_t> buffer;
    uint16_t count = static_cast<uint16_t>(arr.size());
    buffer.insert(buffer.end(),
                  reinterpret_cast<uint8_t*>(&count),
                  reinterpret_cast<uint8_t*>(&count) + sizeof(count));

    for (const auto& s : arr) {
        uint16_t len = static_cast<uint16_t>(s.length());
        buffer.insert(buffer.end(),
                      reinterpret_cast<uint8_t*>(&len),
                      reinterpret_cast<uint8_t*>(&len) + sizeof(len));
        buffer.insert(buffer.end(), s.c_str(), s.c_str() + len);
    }

    prefs_.putBytes(key, buffer.data(), buffer.size());
}

size_t ConfigStorage::getStringArray(const char* key, std::vector<String>& outArr) {
    outArr.clear();
    if (!isKeyValid(key)) { warnInvalidKey(key, "getStringArray"); return 0; }

    size_t storedLen = prefs_.getBytesLength(key);
    if (storedLen < sizeof(uint16_t)) return 0;

    std::vector<uint8_t> buffer(storedLen);
    prefs_.getBytes(key, buffer.data(), storedLen);

    size_t offset = 0;
    uint16_t count = 0;
    memcpy(&count, buffer.data() + offset, sizeof(count));
    offset += sizeof(count);

    for (uint16_t i = 0; i < count; ++i) {
        if (offset + sizeof(uint16_t) > storedLen) break; // пошкоджені/обрізані дані
        uint16_t len = 0;
        memcpy(&len, buffer.data() + offset, sizeof(len));
        offset += sizeof(len);

        if (offset + len > storedLen) break; // пошкоджені/обрізані дані

        String s;
        s.reserve(len);
        for (uint16_t j = 0; j < len; ++j) {
            s += static_cast<char>(buffer[offset + j]);
        }
        offset += len;

        outArr.push_back(s);
    }

    return outArr.size();
}

void ConfigStorage::setFloatArray(const char* key, const float* arr, size_t count) {
    if (!isKeyValid(key)) { warnInvalidKey(key, "setFloatArray"); return; }
    prefs_.putBytes(key, arr, count * sizeof(float));
}

size_t ConfigStorage::getFloatArray(const char* key, float* outArr, size_t maxCount) {
    if (!isKeyValid(key)) { warnInvalidKey(key, "getFloatArray"); return 0; }
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
    if (!isKeyValid(key)) { warnInvalidKey(key, "getType"); return NVS_TYPE_ANY; }
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
    if (!isKeyValid(key)) { warnInvalidKey(key, "peekStructHeader"); return false; }
    size_t storedLen = prefs_.getBytesLength(key);
    if (storedLen < sizeof(BlobHeader)) return false;
    prefs_.getBytes(key, &outHeader, sizeof(BlobHeader));
    return true;
}
