#include "EcoflowAuthClient.hpp"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <time.h>

#include "EcoflowSigner.hpp"

const char *EcoflowAuthClient::kApiHost = "api-e.ecoflow.com";
const char *EcoflowAuthClient::kCertPath = "/iot-open/sign/certification";
const char *EcoflowAuthClient::kDevicePath = "/iot-open/sign/device/list";
const char *EcoflowAuthClient::kQuotaAllPath = "/iot-open/sign/device/quota/all";

EcoflowAuthClient::EcoflowAuthClient(const String &accessKey, const String &secretKey)
    : _accessKey(accessKey), _secretKey(secretKey) {}

String EcoflowAuthClient::generateNonce() const { return String(random(100000, 999999)); }

String EcoflowAuthClient::generateTimestamp() const {
  // Потребує синхронізованого часу (див. configTime() у main.cpp),
  // інакше timestamp буде некоректним і сервер може відхилити підпис.
  return String((uint64_t)time(nullptr) * 1000ULL);
}

bool EcoflowAuthClient::signedGet(const char *path, String &outPayload, const String &query) {
  String nonce = generateNonce();
  String timestamp = generateTimestamp();
  String sign = EcoflowSigner::sign(_accessKey, _secretKey, nonce, timestamp);

  WiFiClientSecure tlsClient;
  tlsClient
      .setInsecure();  // TODO(production): закріпити CA-сертифікат EcoFlow замість setInsecure()

  HTTPClient https;
  String url = String("https://") + kApiHost + path;
  if (query.length() > 0) {
    url += "?" + query;
  }

  if (!https.begin(tlsClient, url)) {
    _lastError = "https.begin() failed";
    return false;
  }

  https.addHeader("Content-Type", "application/json;charset=UTF-8");
  https.addHeader("accessKey", _accessKey);
  https.addHeader("nonce", nonce);
  https.addHeader("timestamp", timestamp);
  https.addHeader("sign", sign);

  int httpCode = https.GET();
  if (httpCode != HTTP_CODE_OK) {
    _lastError = "HTTP GET error, code=" + String(httpCode);
    https.end();
    return false;
  }

  outPayload = https.getString();
  https.end();
  return true;
}

bool EcoflowAuthClient::fetchMqttCredentials(EcoflowMqttCredentials &outCredentials) {
  String payload;
  if (!signedGet(kCertPath, payload)) {
    return false;
  }

  JsonDocument doc;  // ArduinoJson v7
  DeserializationError err = deserializeJson(doc, payload);
  if (err) {
    _lastError = "JSON parse error: " + String(err.c_str());
    return false;
  }

  const char *code = doc["code"] | "";
  if (String(code) != "0") {
    const char *message = doc["message"] | "unknown error";
    _lastError = "EcoFlow API error: " + String(message);
    return false;
  }

  outCredentials.certificateAccount = doc["data"]["certificateAccount"] | "";
  outCredentials.certificatePassword = doc["data"]["certificatePassword"] | "";
  outCredentials.url = doc["data"]["url"] | "";
  outCredentials.port = (uint16_t)(doc["data"]["port"] | 8883);
  outCredentials.protocol = doc["data"]["protocol"] | "mqtts";

  if (!outCredentials.isValid()) {
    _lastError = "API response has no valid MQTT data";
    return false;
  }

  return true;
}

bool EcoflowAuthClient::fetchQuotaAll(const String &serialNumber, JsonDocument &outDoc,
                                     bool &notAllowed) {
  notAllowed = false;

  String payload;
  if (!signedGet(kQuotaAllPath, payload, "sn=" + serialNumber)) {
    return false;
  }

  DeserializationError err = deserializeJson(outDoc, payload);
  if (err) {
    _lastError = "JSON parse error: " + String(err.c_str());
    return false;
  }

  const char *code = outDoc["code"] | "";
  if (String(code) != "0") {
    const char *message = outDoc["message"] | "unknown error";
    // 1006 - пристрій не відкритий для REST-читання (DELTA mini, Smart
    // Generator). Це не збій запиту, а постійна властивість пристрою.
    notAllowed = (String(code) == "1006");
    _lastError = "EcoFlow API error: " + String(message);
    return false;
  }

  return true;
}

bool EcoflowAuthClient::fetchDeviceList(std::vector<EcoflowDevice> &outDevices) {
  outDevices.clear();

  String payload;
  if (!signedGet(kDevicePath, payload)) {
    return false;
  }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, payload);
  if (err) {
    _lastError = "broken JSON: " + String(err.c_str());
    return false;
  }

  const char *code = doc["code"] | "";
  if (String(code) != "0") {
    const char *message = doc["message"] | "unknown error";
    _lastError = "Error EcoFlow API: " + String(message);
    return false;
  }

  JsonArrayConst data = doc["data"].as<JsonArrayConst>();
  if (data.isNull()) {
    _lastError = "Field 'data' is missing or not an array";
    return false;
  }

  for (JsonObjectConst item : data) {
    EcoflowDevice device;
    device.serialNumber = item["sn"] | "";
    device.name = item["deviceName"] | "";
    // "online" приходить числом 1/0.
    device.online = (int)(item["online"] | 0) != 0;

    if (device.serialNumber.length() > 0) {
      outDevices.push_back(device);
    }
  }

  return true;
}
