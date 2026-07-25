#include "EcoFlowSigner.hpp"
#include <mbedtls/md.h>

String EcoFlowSigner::hmacSha256Hex(const String &secretKey, const String &message) {
    unsigned char hmacResult[32];

    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);
    mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 1 /* HMAC */);
    mbedtls_md_hmac_starts(&ctx, (const unsigned char *)secretKey.c_str(), secretKey.length());
    mbedtls_md_hmac_update(&ctx, (const unsigned char *)message.c_str(), message.length());
    mbedtls_md_hmac_finish(&ctx, hmacResult);
    mbedtls_md_free(&ctx);

    String hex;
    hex.reserve(64);
    char byteBuf[3];
    for (int i = 0; i < 32; i++) {
        snprintf(byteBuf, sizeof(byteBuf), "%02x", hmacResult[i]);
        hex += byteBuf;
    }
    return hex;
}

String EcoFlowSigner::buildCanonicalString(const String &accessKey,
                                            const String &nonce,
                                            const String &timestamp,
                                            const std::map<String, String> &extraParams) {
    String canonical;
    // std::map ітерує ключі у відсортованому порядку — саме це вимагає EcoFlow.
    for (const auto &kv : extraParams) {
        canonical += kv.first + "=" + kv.second + "&";
    }
    canonical += "accessKey=" + accessKey + "&";
    canonical += "nonce=" + nonce + "&";
    canonical += "timestamp=" + timestamp;
    return canonical;
}

String EcoFlowSigner::sign(const String &accessKey,
                            const String &secretKey,
                            const String &nonce,
                            const String &timestamp,
                            const std::map<String, String> &extraParams) {
    String canonical = buildCanonicalString(accessKey, nonce, timestamp, extraParams);
    return hmacSha256Hex(secretKey, canonical);
}
