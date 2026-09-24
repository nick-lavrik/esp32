#pragma once

// Розділ "mqtt" веб-порталу: стан ЗАГАЛЬНОГО MQTT-клієнта (mqtt у src/main.cpp -
// console mirror/віддалені команди/LWT, НЕ EcoflowClient - той має свій
// розділ, WebEcoflowModule) + дзеркало консолі (lib/ConsoleMqtt).
//
// CLAUDE.md, розділ "Видимість MQTT-трафіку на веб-порталі": будь-яка нова
// підписка/публікація на mqtt (src/main.cpp, setupMqttClient()) має лишатись
// видимою тут - хоча б через host()/receivedCount()/publishedCount() нижче,
// а не існувати мовчки поза порталом.
//
// ConsoleMqtt - опційний (HAS_CONSOLE_MQTT, явний прапорець з environment.h +
// build-fail у lib/ConsoleMqtt/ConsoleMqtt.hpp): на платах без нього
// конструктор бере лише клієнт, а JSON віддає "consoleMirror":{"available":false}.

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <CommandQueue.hpp>
#include <ConsoleMqtt.hpp>
#include <MqttClient.hpp>

#include "IWebModule.hpp"

class WebMqttModule : public IWebModule {
public:
#if HAS_CONSOLE_MQTT
  WebMqttModule(MqttClient& client, CommandQueue& commandQueue, ConsoleMqtt& consoleMqtt,
                const char* heartbeatMessage, uint32_t heartbeatIntervalMs)
      : _client(client), _commandQueue(commandQueue), _consoleMqtt(&consoleMqtt),
        _heartbeatMessage(heartbeatMessage), _heartbeatIntervalMs(heartbeatIntervalMs) {}
#else
  WebMqttModule(MqttClient& client, CommandQueue& commandQueue, const char* heartbeatMessage,
                uint32_t heartbeatIntervalMs)
      : _client(client), _commandQueue(commandQueue), _heartbeatMessage(heartbeatMessage),
        _heartbeatIntervalMs(heartbeatIntervalMs) {}
#endif

  const char* name() const override { return "mqtt"; }
  void registerRoutes(AsyncWebServer& server, WebPortal& portal) override;

  // Той самий JSON, що й /api/mqtt/status - метод, не лише роут: MQTT
  // SAPI-канал (mqtt-status, src/main.cpp, docs/mqtt-web-handoff.md) бере ці
  // ж поля напряму, без другого будівельника того самого об'єкта (DRY,
  // CLAUDE.md - той самий патерн, що вже застосований до WebPortal::statusJson()).
  String statusJson() const;

private:
  MqttClient& _client;
  // CommandQueue - спільна для serial/web/MQTT/cron (lib/CommandQueue), не
  // власність цього розділу. rejected() показуємо тут разом з рештою
  // "може мовчки відмовити" лічильників (CLAUDE.md, "Видимість MQTT-
  // трафіку") - той самий хаб видимості, що вже є для droppedOutgoing/
  // droppedIncoming/subscribeDenied, а не окрема вкладка під одну цифру.
  CommandQueue& _commandQueue;
#if HAS_CONSOLE_MQTT
  ConsoleMqtt* _consoleMqtt = nullptr;
#endif
  // "Доказ життя" в LWT-топік між (пере)з'єднаннями - окремо від самого LWT
  // (offline/online). Значення приходять із src/main.cpp (там само, де
  // зареєстрований cron-таск, що фактично публікує) - щоб сторінка не могла
  // мовчки розійтись зі справжнім інтервалом/повідомленням (DRY).
  const char* _heartbeatMessage = nullptr;
  uint32_t _heartbeatIntervalMs = 0;
};
