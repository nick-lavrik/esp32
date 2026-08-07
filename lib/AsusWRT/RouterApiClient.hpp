#pragma once

#include <Arduino.h>

#include "IRouterApiClient.hpp"

// HTTP (без TLS) auth-клієнт для ASUS/Merlin-style роутерів.
// Відтворює послідовність:
//   curl login.cgi --data-raw 'login_authorization=...' --cookie-jar cookies.jar
//   curl 'appGet.cgi?hook=get_clientlist()' --cookie cookies.jar
//
// Приклад використання:
//   RouterApiClient api("192.168.28.1", "QWRtaW46cGFzcw==");
//   if (!api.login()) { Logger::error("router login failed: %s", api.lastError()); return; }
//   String json;
//   if (!api.fetchClientListJson(json)) { Logger::error("%s", api.lastError()); return; }
//
// host та loginAuthorization передаються ЗЗОВНІ (secrets.ini / ConfigStorage) —
// клас не містить креденшлів як констант.
class RouterApiClient : public IRouterApiClient {
public:
  RouterApiClient(const String& host, const String& loginAuthorization);

  bool login() override;
  bool fetchClientListJson(String& outJson) override;
  const char* lastError() const override { return _lastError.c_str(); }

private:
  String _host;
  String _loginAuthorization;
  String _sessionCookie;
  String _lastError;

  // Витягує ім'я=значення з першого "Set-Cookie" заголовка відповіді (без атрибутів
  // типу Path/HttpOnly) — саме так, як curl --cookie-jar зберігає пару в cookies.jar.
  static String extractCookiePair(const String& setCookieHeader);
};
