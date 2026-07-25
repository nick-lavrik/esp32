#include "EcoFlowAuthClient.h"
#include "EcoFlowSigner.h"

#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>

const char *EcoFlowAuthClient::kApiHost = "api-e.ecoflow.com";
const char *EcoFlowAuthClient::kCertPath = "/iot-open/sign/certification";

EcoFlowAuthClient::EcoFlowAuthClient(const String &accessKey, const String &secretKey)
    : _accessKey(accessKey), _secretKey(secretKey) {}

String EcoFlowAuthClient::generateNonce() const {
    return String(random(100000, 999999));
}

String EcoFlowAuthClient::generateTimestamp() const {
    // Потребує синхронізованого часу (див. configTime() у main.cpp),
    // інакше timestamp буде некоректним і сервер може відхилити підпис.
    return String((uint64_t)time(nullptr) * 1000ULL);
}

bool EcoFlowAuthClient::fetchMqttCredentials(EcoFlowMqttCredentials &outCredentials) {
    String nonce = generateNonce();
    String timestamp = generateTimestamp();
    String sign = EcoFlowSigner::sign(_accessKey, _secretKey, nonce, timestamp);

    WiFiClientSecure tlsClient;
    tlsClient.setInsecure();  // TODO(production): закріпити CA-сертифікат EcoFlow замість setInsecure()

    HTTPClient https;
    String url = String("https://") + kApiHost + kCertPath;

    if (!https.begin(tlsClient, url)) {
        _lastError = "https.begin() не вдався";
        return false;
    }

    https.addHeader("Content-Type", "application/json;charset=UTF-8");
    https.addHeader("accessKey", _accessKey);
    https.addHeader("nonce", nonce);
    https.addHeader("timestamp", timestamp);
    https.addHeader("sign", sign);

    int httpCode = https.GET();
    if (httpCode != HTTP_CODE_OK) {
        _lastError = "HTTP GET помилка, код=" + String(httpCode);
        https.end();
        return false;
    }

    String payload = https.getString();
    https.end();

    JsonDocument doc;  // ArduinoJson v7
    DeserializationError err = deserializeJson(doc, payload);
    if (err) {
        _lastError = "Помилка розбору JSON: " + String(err.c_str());
        return false;
    }

    const char *code = doc["code"] | "";
    if (String(code) != "0") {
        const char *message = doc["message"] | "невідома помилка";
        _lastError = "Помилка EcoFlow API: " + String(message);
        return false;
    }

    outCredentials.certificateAccount = doc["data"]["certificateAccount"] | "";
    outCredentials.certificatePassword = doc["data"]["certificatePassword"] | "";
    outCredentials.url = doc["data"]["url"] | "";
    outCredentials.port = (uint16_t)(doc["data"]["port"] | 8883);
    outCredentials.protocol = doc["data"]["protocol"] | "mqtts";

    if (!outCredentials.isValid()) {
        _lastError = "Відповідь API не містить коректних MQTT-даних";
        return false;
    }

    return true;
}
