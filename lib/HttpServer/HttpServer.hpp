#pragma once

// HttpServer — обгортка над ESPAsyncWebServer (mathieucarbou fork).
//
// Названо HttpServer, а не WebServer: у arduino-esp32 core вже є вбудована
// (синхронна) бібліотека з класом WebServer
// (framework-arduinoespressif32/libraries/WebServer/) — PlatformIO LDF
// підтягує обидві версії при однаковому імені, лінкер падає з
// "multiple definition". Тому в цьому проєкті ім'я класу навмисно інше.
//
// Призначення:
//   - єдина точка входу для HTTP-сервера в проєкті;
//   - статика (HTML/CSS/JS) віддається не напряму з конкретного fs::FS,
//     а через IStaticSource* — це дозволяє підміняти джерело
//     (LittleFS / SD / PROGMEM / callback / ConfigStorage) або комбінувати
//     їх у CompositeStaticSource без зміни коду HttpServer;
//   - шаблонізація (%KEY%) також винесена в ITemplateResolver* —
//     HttpServer лише підключає резолвер до setTemplateProcessor()
//     штатного ESPAsyncWebServer, сам не знає деталей підстановки;
//   - EventDispatcher — опціональний, для публікації подій сервера
//     ("HttpServer.started" / "HttpServer.stopped") в загальну шину подій
//     проєкту. Контракт: IEventDispatcher::dispatch(IEvent&, eventName),
//     події наслідують Event (lib/EventDispatcher/Event.hpp), приклад
//     реального виклику - AnalogSensorEventDispatcher::update()
//     (`_eventDispatcher.dispatch(event, EVT_NAME);`).
//
// Board-specific:
//   - ESP32 (класичний) і ESP32-S3 — обидва мають достатньо ресурсів для
//     AsyncTCP, різниця лише в дефолтних лімітах (див. HttpServerConfig).
//   - ESP8266 — інший TCP-стек (ESPAsyncTCP-esphome), тому нижче є
//     розгалуження по типу транспорту. Сам клас HttpServer лишається
//     платформо-незалежним, розгалуження ізольовані в .cpp.
//   - BOARD_HAS_SD — визначається в build_flags плати, де фізично є SD-картка
//     (наразі esp32-st7789, 4848s040 з SD-модулем). Сам HttpServer.hpp
//     не залежить від SD напряму — це відповідальність конкретного
//     IStaticSource (SdStaticSource), який реєструється зовні.
//
// Приклад використання (Крок 1 — статика реалізована, шаблонізація ще ні):
//
//   #include "HttpServer.hpp"
//   #include "CompositeStaticSource.hpp"
//   #include "LittleFsStaticSource.hpp"
//
//   LittleFsStaticSource littleFsSource(LittleFS);
//   CompositeStaticSource staticSources;
//   staticSources.addSource(&littleFsSource);
//
//   HttpServer httpServer(HttpServerConfig{});
//   httpServer.setStaticSource(&staticSources);
//   httpServer.setEventDispatcher(&eventDispatcher);
//   httpServer.begin();
//
//   // у loop() виклик не потрібен — ESPAsyncWebServer асинхронний,
//   // обробка йде у своєму FreeRTOS-таску (AsyncTCP).

#include <cstdint>

#if defined(ESP32)
#include <ESPAsyncWebServer.h>
#elif defined(ESP8266)
#include <ESPAsyncWebServer.h>
#else
#error "HttpServer library supports ESP32 and ESP8266 only"
#endif

class IStaticSource;
class ITemplateResolver;
class IEventDispatcher;  // lib/EventDispatcher/IEventDispatcher.hpp

// Конфігурація ліміту з'єднань/буферів — значення за замовчуванням
// відрізняються по платформі через різницю у вільному heap.
struct HttpServerConfig {
  uint16_t port = 80;

#if defined(ESP32)
  uint8_t maxClients =
      4;  // ESP32 classic / S3 — обмежуємо явно, не покладаємось на дефолт бібліотеки
  size_t chunkSize = 1024;  // розмір чанку при читанні файлів з SD/LittleFS
#elif defined(ESP8266)
  uint8_t maxClients = 2;  // менше RAM — менше одночасних з'єднань
  size_t chunkSize = 512;
#endif
};

class HttpServer {
public:
  explicit HttpServer(const HttpServerConfig& config = HttpServerConfig());
  ~HttpServer();

  // Заборона копіювання — власник ресурсу AsyncWebServer.
  HttpServer(const HttpServer&) = delete;
  HttpServer& operator=(const HttpServer&) = delete;

  // Джерело статики. nullptr дозволено (сервер підніметься без статичних роутів,
  // корисно якщо потрібні лише API/JSON-ендпоінти).
  void setStaticSource(IStaticSource* staticSource);

  // Резолвер для %KEY% підстановок у текстових файлах (html/css/js).
  // Якщо не заданий — template processor не підключається, файли віддаються as-is.
  void setTemplateResolver(ITemplateResolver* templateResolver);

  // Опціональна публікація подій сервера в спільну шину проєкту.
  // Pointer injection за прийнятим у проєкті паттерном (nullptr за замовчуванням + setter).
  void setEventDispatcher(IEventDispatcher* eventDispatcher);

  // Піднімає сервер (реєструє роути, викликає AsyncWebServer::begin()).
  bool begin();

  // Зупиняє сервер, звільняє AsyncWebServer.
  void end();

  bool isRunning() const { return _isRunning; }

private:
  HttpServerConfig _config;
  AsyncWebServer _server;
  IStaticSource* _staticSource = nullptr;
  ITemplateResolver* _templateResolver = nullptr;
  IEventDispatcher* _eventDispatcher = nullptr;
  bool _isRunning = false;
};
