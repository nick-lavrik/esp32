#include "RouterApiClient.hpp"

#if ESP8266
#include <ESP8266HTTPClient.h>
#else // if ESP32
#include <HTTPClient.h>
#endif

#include <WiFiClient.h>

RouterApiClient::RouterApiClient(const String& host, const String& loginAuthorization)
    : _host(host), _loginAuthorization(loginAuthorization) {}

String RouterApiClient::extractCookiePair(const String& setCookieHeader) {
  // "session_id=abcd1234; path=/; HttpOnly" -> "session_id=abcd1234"
  int semicolon = setCookieHeader.indexOf(';');
  if (semicolon < 0) return setCookieHeader;
  return setCookieHeader.substring(0, semicolon);
}

bool RouterApiClient::login() {
  WiFiClient client;
  HTTPClient http;

  String url = String("http://") + _host + "/login.cgi";
  if (!http.begin(client, url)) {
    _lastError = "http.begin() не вдався (login)";
    return false;
  }

  http.addHeader("Referer", String("http://") + _host + "/Main_Login.asp");
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");

  // ОБОВ'ЯЗКОВО до POST: HTTPClient зберігає значення лише тих заголовків
  // відповіді, які заявлені через collectHeaders(). Без цього виклику
  // http.header("Set-Cookie") нижче ЗАВЖДИ повертає порожній рядок, тобто
  // login() не міг завершитись успіхом за жодних умов.
  static const char* kCollectedHeaders[] = {"Set-Cookie"};
  http.collectHeaders(kCollectedHeaders, 1);

  String body = "login_authorization=" + _loginAuthorization;
  int httpCode = http.POST(body);

  if (httpCode <= 0) {
    _lastError = String("POST login.cgi мережева помилка: ") + http.errorToString(httpCode);
    http.end();
    return false;
  }

  // curl-скрипт ігнорує тіло відповіді (> /dev/null) і покладається лише на cookie.
  String setCookie = http.header("Set-Cookie");
  http.end();

  if (setCookie.isEmpty()) {
    _lastError = "login.cgi не повернув Set-Cookie — перевір login_authorization";
    return false;
  }

  _sessionCookie = extractCookiePair(setCookie);
  return true;
}

bool RouterApiClient::fetchClientListJson(String& outJson) {
  if (_sessionCookie.isEmpty()) {
    _lastError = "fetchClientListJson() викликано до успішного login()";
    return false;
  }

  WiFiClient client;
  HTTPClient http;

  String url = String("http://") + _host + "/appGet.cgi?hook=get_clientlist()";
  if (!http.begin(client, url)) {
    _lastError = "http.begin() не вдався (appGet.cgi)";
    return false;
  }

  http.addHeader("Referer", String("http://") + _host + "/index.asp");
  http.addHeader("X-Requested-With", "XMLHttpRequest");
  http.addHeader("Cookie", _sessionCookie);

  int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) {
    _lastError = String("GET appGet.cgi HTTP код=") + httpCode;
    http.end();
    return false;
  }

  outJson = http.getString();
  http.end();
  return true;
}
