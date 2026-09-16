#pragma once

// Розділ "ecoflow" веб-порталу: стан акаунта EcoFlow (MQTT-діагностика +
// телеметрія кожного пристрою з EcoflowDeviceRegistry).
//
// Живе тут, а не в lib/WebPortal поруч з рештою Web*Module - EcoflowClient і
// EcoflowDeviceRegistry лежать у src/Ecoflow, а заголовок із src/ у
// бібліотеку не заінклюдиш (LDF не додає src/ у include path lib/, CLAUDE.md,
// розділ DRY). Сам модуль лишається звичайним IWebModule і реєструється
// в WebPortal так само, як лишні розділи - лише файл лежить поруч із тим, що
// він показує.
//
// Розділ лише читає - жодної мутації, тому без WebJobQueue: усе, що тут
// віддається, вже лежить у RAM, і побудова JSON у таску AsyncTCP так само
// безпечна, як у WebScreenModule.
//
// Модуль реєструється лише на env, де HAS_ECOFLOW_CLIENT увімкнено (див.
// src/main.cpp) - сторінка ховає вкладку сама, якщо "ecoflow" немає у списку
// "modules" з /api/status.

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <IWebModule.hpp>

#include "EcoflowClient.hpp"
#include "EcoflowDeviceRegistry.hpp"

class WebEcoflowModule : public IWebModule {
public:
  WebEcoflowModule(EcoflowClient& client, EcoflowDeviceRegistry& registry)
      : _client(client), _registry(registry) {}

  const char* name() const override { return "ecoflow"; }
  void registerRoutes(AsyncWebServer& server, WebPortal& portal) override;

private:
  EcoflowClient& _client;
  EcoflowDeviceRegistry& _registry;
};
