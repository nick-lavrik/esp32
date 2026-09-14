#pragma once

// Розділ веб-порталу (WiFi, консоль, команди, LittleFS, NVS, екран).
//
// Кожен розділ сам реєструє свої роути і сам вирішує, що з цього безпечно
// зробити прямо в таску сервера, а що треба віддати в WebJobQueue. WebPortal
// про вміст розділів нічого не знає - він лише транспорт, автентифікація і
// статика.
//
// Час життя модулів - на боці власника (main.cpp плати), портал їх не видаляє:
// той самий контракт, що в CompositeStaticSource щодо IStaticSource.

#include <Arduino.h>

class AsyncWebServer;
class WebPortal;

class IWebModule {
public:
  virtual ~IWebModule() = default;

  // Коротке ім'я розділу для /api/status ("wifi", "console", ...).
  virtual const char* name() const = 0;

  // Викликається з WebPortal::begin() ДО HttpServer::begin().
  virtual void registerRoutes(AsyncWebServer& server, WebPortal& portal) = 0;

  // Періодична робота в контексті loop() (не в таску сервера). За
  // замовчуванням нічого: більшості розділів вистачає WebJobQueue.
  virtual void loop() {}
};
