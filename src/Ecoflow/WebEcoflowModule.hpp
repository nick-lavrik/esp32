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

#include <vector>

#include "EcoflowClient.hpp"
#include "EcoflowDeviceRegistry.hpp"

// Як часто перебудовувати знімок - сторінка й так опитує раз на 5 с
// (poller(refreshEcoflow, 5000, ...), assets/www/index.html), частіше немає
// сенсу.
#ifndef WEB_ECOFLOW_SNAPSHOT_INTERVAL_MS
#define WEB_ECOFLOW_SNAPSHOT_INTERVAL_MS 1000
#endif

// Провайдер: сирий структурний знімок клієнта EcoFlow (не готовий JSON) -
// docs/mqtt-web-handoff.md, "Провайдер ≠ форматер".
struct WebEcoflowStatus {
  bool connected = false;
  bool running = false;
  String channel;
  String account;
  String brokerHost;
  uint16_t brokerPort = 0;
  bool viaProxy = false;
  uint32_t messageCount = 0;
  String lastTopic;
  String lastError;
  uint32_t heapFreeBytes = 0;
  uint32_t heapLargestBlockBytes = 0;
  uint32_t netStackHeadroomBytes = 0;
  // Момент зняття знімка - похідні поля пристроїв (ageMs/gridForMs) рахує
  // форматер ВІД ЦИХ значень, а не "живими" millis()/time(nullptr) у собі:
  // ті виклики самі по собі безпечні, але дали б різні числа для різних
  // пристроїв в одній відповіді, хоча знімок один.
  uint32_t snapshotMs = 0;
  time_t snapshotEpoch = 0;
};

// Провайдер: сирий структурний знімок ОДНОГО пристрою - копія
// EcoflowDeviceState (уже структура, не String) плюс два похідні значення з
// журналу (gridSinceEpoch/totalChangeCount), зняті в той самий момент, що й
// решта. EcoflowGridJournal сам по собі лишається небезпечним для читання
// поза loop() (коментар класу нижче) - тому значення з нього копіюються тут,
// а не пізніше через збережений вказівник.
struct WebEcoflowDeviceSnapshot {
  EcoflowDeviceState state;
  time_t gridSinceEpoch = 0;
  uint32_t gridChangeCount = 0;
};

class WebEcoflowModule : public IWebModule {
public:
  WebEcoflowModule(EcoflowClient& client, EcoflowDeviceRegistry& registry);
  ~WebEcoflowModule() override;

  const char* name() const override { return "ecoflow"; }
  void registerRoutes(AsyncWebServer& server, WebPortal& portal) override;
  void loop() override;

  // Провайдер: копії знімків під тим самим мьютексом, що пише loop() -
  // основа для будь-якого форматера (portal нижче; MQTT - коли з'явиться
  // команда 'ecoflow-status').
  WebEcoflowStatus statusSnapshot() {
    Lock lock(_mutex);
    return _status;
  }
  std::vector<WebEcoflowDeviceSnapshot> devicesSnapshot() {
    Lock lock(_mutex);
    return _devices;
  }

  // Форматер порталу - той самий JSON, що й /api/ecoflow/status раніше
  // (перевірено на живому пристрої при рефакторингу).
  static String portalStatusJson(const WebEcoflowStatus& status,
                                  const std::vector<WebEcoflowDeviceSnapshot>& devices);

  // Форматер MQTT-команди 'ecoflow-status' (фаза 1, docs/mqtt-web-handoff.md).
  // Відрізняється від portalStatusJson() РІВНО одним - без сирого "params"
  // (може бути 353 поля на пристрій із captureAll, десятки КБ, розділ
  // "Розмір payload" документа: MQTT-payload дорожчий за HTTP-відповідь).
  // Решта полів пристрою (grid/soc/watts тощо) - ті самі.
  static String mqttStatusJson(const WebEcoflowStatus& status,
                                const std::vector<WebEcoflowDeviceSnapshot>& devices);

private:
  // Знімає провайдерські структури. Викликається лише з loop() - див.
  // коментар класу вище (trackedParams/EcoflowGridJournal небезпечні поза
  // ним).
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
  WebEcoflowStatus _status;
  std::vector<WebEcoflowDeviceSnapshot> _devices;
  uint32_t _lastSnapshotMs = 0;

#if defined(ESP32)
  SemaphoreHandle_t _mutex = nullptr;
#else
  int _mutex = 0;
#endif
};
