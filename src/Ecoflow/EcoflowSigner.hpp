#pragma once

#include <Arduino.h>
#include <map>

// Формує підпис (sign) для запитів до EcoFlow Open Platform REST API:
// HMAC-SHA256(secretKey, canonicalString), hex у нижньому регістрі.
//
// canonicalString = "accessKey=...&nonce=...&timestamp=..."
//
// ВАЖЛИВО: query-параметри запиту (напр. ?sn=...) у підпис НЕ входять -
// перевірено на живому API. Спроби додати їх (перед accessKey, після нього,
// або відсортувавши все разом) стабільно дають 8521 "signature is wrong",
// тоді як підпис лише з accessKey/nonce/timestamp приймається і сервер
// відповідає по суті. extraParams лишено в сигнатурі на випадок, якщо
// POST-ендпоінти EcoFlow вимагатимуть іншої схеми - для GET його НЕ вживати."
class EcoflowSigner {
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
