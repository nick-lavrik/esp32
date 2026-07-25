#pragma once

#include <Arduino.h>
#include <map>

// Формує підпис (sign) для запитів до EcoFlow Open Platform REST API
// відповідно до їхньої схеми: HMAC-SHA256(secretKey, canonicalString), hex у нижньому регістрі.
//
// canonicalString = [відсортовані "key=value" з extraParams через '&'] +
//                    "accessKey=...&nonce=...&timestamp=..."
class EcoFlowSigner {
public:
    // Обчислює HMAC-SHA256(message, key) і повертає результат у вигляді hex-рядка (64 символи).
    static String hmacSha256Hex(const String &secretKey, const String &message);

    // Будує канонічний рядок і підписує його secretKey.
    // extraParams — додаткові параметри запиту (наприклад, "sn" для команд конкретного пристрою).
    // Для простих запитів без параметрів (наприклад, отримання MQTT-сертифікату) залишити порожнім.
    static String sign(const String &accessKey,
                        const String &secretKey,
                        const String &nonce,
                        const String &timestamp,
                        const std::map<String, String> &extraParams = {});

private:
    static String buildCanonicalString(const String &accessKey,
                                        const String &nonce,
                                        const String &timestamp,
                                        const std::map<String, String> &extraParams);
};
