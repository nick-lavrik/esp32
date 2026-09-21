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
// Той самий патерн, що WebWifiModule (див. коментар там) - і з тієї самої
// причини.
//
// ЧИТАННЯ (/api/ecoflow/status) віддається з ГОТОВОГО знімка, який оновлює
// loop(). Причина не в швидкості, а в тому, що EcoflowDeviceState::trackedParams
// (std::map) мутує EcoflowClient::loop() (main.cpp викликає його з того
// самого loop(), що й WebPortal::loop() -> WebEcoflowModule::loop()) без
// жодного мьютекса. Раніше JSON будувався прямо в таску AsyncTCP і час від
// часу віддавав сміття: обхід std::map під час вставки нового ключа тим
// самим деревом - undefined behavior, а не просто застаріле значення.
// WebJobQueue тут навмисно НЕ використовується - його Result::text копіює
// великий JSON кілька разів (slot -> result -> outResult -> відповідь
// /api/job), а на платі з фрагментованим heap (~14 КБ найбільший вільний
// блок на esp32-st7789) String::concat() мовчки провалюється і віддає
// порожній "result". Кешований String, що лише МІНЯЄТЬСЯ місцями (std::move)
// під коротким Lock, такої проблеми не має - той самий підхід, що і в
// WebWifiModule.
//
// Модуль реєструється лише на env, де HAS_ECOFLOW_CLIENT увімкнено (див.
// src/main.cpp) - сторінка ховає вкладку сама, якщо "ecoflow" немає у списку
// "modules" з /api/status.

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <IWebModule.hpp>

#if defined(ESP32)
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#endif

#include "EcoflowClient.hpp"
#include "EcoflowDeviceRegistry.hpp"

// Як часто перебудовувати знімок - сторінка й так опитує раз на 5 с
// (poller(refreshEcoflow, 5000, ...), assets/www/index.html), частіше немає
// сенсу.
#ifndef WEB_ECOFLOW_SNAPSHOT_INTERVAL_MS
#define WEB_ECOFLOW_SNAPSHOT_INTERVAL_MS 1000
#endif

class WebEcoflowModule : public IWebModule {
public:
  WebEcoflowModule(EcoflowClient& client, EcoflowDeviceRegistry& registry);
  ~WebEcoflowModule() override;

  const char* name() const override { return "ecoflow"; }
  void registerRoutes(AsyncWebServer& server, WebPortal& portal) override;
  void loop() override;

private:
  // Збирає JSON знімок. Викликається лише з loop() - див. коментар вище.
  void _refreshSnapshot();

  class Lock {
  public:
#if defined(ESP32)
    explicit Lock(SemaphoreHandle_t m) : _m(m) {
      if (_m) xSemaphoreTake(_m, portMAX_DELAY);
    }
    ~Lock() {
      if (_m) xSemaphoreGive(_m);
    }

  private:
    SemaphoreHandle_t _m;
#else
    explicit Lock(int) {}
#endif
    Lock(const Lock&) = delete;
    Lock& operator=(const Lock&) = delete;
  };

  EcoflowClient& _client;
  EcoflowDeviceRegistry& _registry;

  // Знімок: пишеться з loop(), читається з таска сервера - звідси мьютекс.
  String _statusJson = "{}";
  uint32_t _lastSnapshotMs = 0;

#if defined(ESP32)
  SemaphoreHandle_t _mutex = nullptr;
#else
  int _mutex = 0;
#endif
};
