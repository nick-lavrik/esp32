#pragma once

#include <Arduino.h>

// Інтерфейс auth+fetch клієнта роутера (ASUS/Merlin-style login.cgi + appGet.cgi).
class IRouterApiClient {
public:
  virtual ~IRouterApiClient() = default;

  // POST на login.cgi, зберігає session cookie для наступних запитів.
  // Повертає false при мережевій помилці або неуспішному логіні.
  virtual bool login() = 0;

  // GET appGet.cgi?hook=get_clientlist() з уже збереженою cookie.
  // outJson заповнюється сирим тілом відповіді (JSON) при успіху.
  // Повертає false при мережевій помилці (у т.ч. якщо login() ще не викликався).
  virtual bool fetchClientListJson(String& outJson) = 0;

  // Текст останньої помилки (мережевої або HTTP-коду), для логування викликаючою стороною.
  virtual const char* lastError() const = 0;
};
