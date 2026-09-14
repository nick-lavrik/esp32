#pragma once

// Розділ "wifi": стан мережі, збережені профілі, скан ефіру, підключення,
// точка доступу.
//
// Два різні шляхи, і різниця між ними принципова.
//
// ЧИТАННЯ (/api/wifi/status, /api/wifi/connections) віддається з ГОТОВОГО
// знімка, який оновлює loop(). Причина не в швидкості: NetworkSupervisor
// тримає список з'єднань під власним мьютексом і чіпає його з трьох тасків,
// а connections() повертає посилання на вектор - читати його з таска сервера
// означало б тримати сире посилання на дані, які інший таск може
// перевиділити. Знімок знімається там само, де живуть інші команди
// (loop()), і віддається як звичайний рядок.
//
// МУТАЦІЇ (скан, connect, hotspot, збереження профілю) йдуть через
// WebJobQueue, тобто виконуються в loop(). NetworkSupervisor::scan() блокує
// на 2-3 секунди, connectTo()/startAp() смикають блокуючі WiFi.*, а
// saveConfig() пише в NVS. У таску сервера це зупинило б сам сервер, а
// WiFi-виклики ризикують дедлоком, описаним у NetworkSupervisor.hpp.

#include <Arduino.h>
#include <NetworkSupervisor.hpp>

#if defined(ESP32)
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#endif

#include "IWebModule.hpp"

// Як часто оновлювати знімок стану. 1 с: швидше не має сенсу (FSM і так
// міняє стан повільніше), повільніше - помітно для вкладки, що чекає
// на результат підключення.
#ifndef WEB_WIFI_SNAPSHOT_INTERVAL_MS
#define WEB_WIFI_SNAPSHOT_INTERVAL_MS 1000
#endif

class WebWifiModule : public IWebModule {
public:
  explicit WebWifiModule(NetworkSupervisor& supervisor);
  ~WebWifiModule() override;

  const char* name() const override { return "wifi"; }
  void registerRoutes(AsyncWebServer& server, WebPortal& portal) override;
  void loop() override;

private:
  // Збирає JSON стану і списку профілів. Викликається лише з loop().
  void _refreshSnapshot();

  // Робить скан ефіру і повертає JSON зі списком мереж. Лише з черги задач:
  // блокує на 2-3 секунди.
  String _scanJob();

  // Знаходить профіль за ssid; nullptr - немає. Лише з loop()/черги.
  WifiConnection* _findBySsid(const String& ssid);

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

  NetworkSupervisor& _supervisor;

  // Знімок: пишеться з loop(), читається з таска сервера - звідси мьютекс.
  String _statusJson = "{}";
  String _connectionsJson = "[]";
  uint32_t _lastSnapshotMs = 0;

#if defined(ESP32)
  SemaphoreHandle_t _mutex = nullptr;
#else
  int _mutex = 0;
#endif
};
