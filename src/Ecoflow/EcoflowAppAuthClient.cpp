#include "EcoflowAppAuthClient.hpp"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <mbedtls/base64.h>

#include <vector>

const char *EcoflowAppAuthClient::kApiHost = "api.ecoflow.com";

EcoflowAppAuthClient::EcoflowAppAuthClient(const String &email, const String &password)
    : _email(email), _password(password) {}

String EcoflowAppAuthClient::base64Encode(const String &value) {
  size_t needed = 0;
  // Перший виклик з нульовим буфером повертає потрібний розмір.
  mbedtls_base64_encode(nullptr, 0, &needed, (const unsigned char *)value.c_str(),
                        value.length());
  if (needed == 0) {
    return String();
  }

  String out;
  out.reserve(needed + 1);
  // Пишемо напряму в буфер String: зайва копія тут ні до чого.
  std::vector<unsigned char> buffer(needed + 1, 0);
  size_t written = 0;
  if (mbedtls_base64_encode(buffer.data(), buffer.size(), &written,
                            (const unsigned char *)value.c_str(), value.length()) != 0) {
    return String();
  }
  buffer[written] = '\0';
  return String((const char *)buffer.data());
}

bool EcoflowAppAuthClient::login(String &outToken) {
  WiFiClientSecure tlsClient;
  tlsClient.setInsecure();  // TODO(production): закріпити CA-сертифікат

  HTTPClient https;
  const String url = String("https://") + kApiHost + "/auth/login";
  if (!https.begin(tlsClient, url)) {
    _lastError = "https.begin() failed (login)";
    return false;
  }
  https.addHeader("Content-Type", "application/json");

  JsonDocument request;
  request["email"] = _email;
  request["password"] = base64Encode(_password);
  request["scene"] = "IOT_APP";
  request["userType"] = "ECOFLOW";
  request["os"] = "android";

  String body;
  serializeJson(request, body);

  const int httpCode = https.POST(body);
  if (httpCode != HTTP_CODE_OK) {
    _lastError = "login HTTP error, code=" + String(httpCode);
    https.end();
    return false;
  }

  const String payload = https.getString();
  https.end();

  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, payload);
  if (err) {
    _lastError = "login JSON parse error: " + String(err.c_str());
    return false;
  }

  const char *code = doc["code"] | "";
  if (String(code) != "0") {
    const char *message = doc["message"] | "unknown error";
    _lastError = "login rejected: " + String(message);
    return false;
  }

  outToken = doc["data"]["token"] | "";
  _userId = doc["data"]["user"]["userId"] | "";
  if (outToken.length() == 0) {
    _lastError = "login response has no token";
    return false;
  }
  return true;
}

bool EcoflowAppAuthClient::certification(const String &token,
                                        EcoflowMqttCredentials &outCredentials) {
  WiFiClientSecure tlsClient;
  tlsClient.setInsecure();

  HTTPClient https;
  const String url = String("https://") + kApiHost + "/iot-auth/app/certification";
  if (!https.begin(tlsClient, url)) {
    _lastError = "https.begin() failed (certification)";
    return false;
  }
  https.addHeader("lang", "en_US");
  https.addHeader("authorization", "Bearer " + token);

  const int httpCode = https.GET();
  if (httpCode != HTTP_CODE_OK) {
    _lastError = "certification HTTP error, code=" + String(httpCode);
    https.end();
    return false;
  }

  const String payload = https.getString();
  https.end();

  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, payload);
  if (err) {
    _lastError = "certification JSON parse error: " + String(err.c_str());
    return false;
  }

  const char *code = doc["code"] | "";
  if (String(code) != "0") {
    const char *message = doc["message"] | "unknown error";
    _lastError = "certification rejected: " + String(message);
    return false;
  }

  outCredentials.certificateAccount = doc["data"]["certificateAccount"] | "";
  outCredentials.certificatePassword = doc["data"]["certificatePassword"] | "";
  outCredentials.url = doc["data"]["url"] | "";
  // port тут приходить РЯДКОМ ("8883"), на відміну від Open Platform, де число.
  outCredentials.port = (uint16_t)String(doc["data"]["port"] | "8883").toInt();
  outCredentials.protocol = doc["data"]["protocol"] | "mqtts";

  if (!outCredentials.isValid()) {
    _lastError = "certification response has no valid MQTT data";
    return false;
  }
  return true;
}

bool EcoflowAppAuthClient::fetchMqttCredentials(EcoflowMqttCredentials &outCredentials) {
  if (_email.length() == 0 || _password.length() == 0) {
    _lastError = "email/password not set";
    return false;
  }

  String token;
  if (!login(token)) {
    return false;
  }
  return certification(token, outCredentials);
}
