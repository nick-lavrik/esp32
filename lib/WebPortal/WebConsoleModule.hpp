#pragma once

// Розділ "console": потік логу в браузер і запуск serial-команд звідти.
//
// Потік логу. Власного буфера тут НЕМАЄ - модуль читає кільце журналу
// (lib/Journal) напряму, за тим самим наскрізним seq. До етапу 4 поруч жив
// WebLogBuffer: копія тих самих рядків із власною нумерацією, 60x160 = 9.6 КБ
// у .bss. Кільце вже зберігало те саме, тому копія була чистим дублюванням -
// і зникла разом із мьютексом, підпискою та класом.
//
// Два способи дістати цей потік, обидва курсорні й обидва по тому самому seq:
//
//   GET /api/console/stream  - SSE. Кадр має поле id:, туди йде seq. При обриві
//                              браузер сам перепідключається і надсилає
//                              Last-Event-ID, тобто робить те саме, що ?since=,
//                              без нашої участі.
//   GET /api/console/log?since=<seq> - опитування. ЛИШАЄТЬСЯ як відкат: SSE
//                              через AP-режим або кепський лінк може не
//                              піднятись, а цей ендпойнт уже працює.
//
// Глибина історії в обох випадках - розмір кільця (JOURNAL_RING, див.
// platformio.ini), а не окрема константа: відстав більше - видно з розриву в
// "from" (опитування) або в id (SSE).
//
// SSE віддається З loop() - і з таска помпи журналу, і з таска AsyncTCP тут
// не надсилається НІЧОГО. Це навмисно: помпа має
// лишатись швидкою, а тут кожен кадр - алокація в купі під std::shared_ptr<String>
// на кожного клієнта. Той самий профіль уже кусав на C6 (див. пам'ять про купу),
// тому є два обмежувачі: kSseFramesPerLoop на тік і kSseMaxQueued на глибину
// черги клієнта - якщо він не встигає, ми просто не додаємо йому роботи.
//
// ОБМЕЖЕННЯ, про яке варто знати: у журнал потрапляє лише те, що йде через
// Logger/TLogger. Прямі Serial.print* з коду в нього не заходять - у
// веб-консолі їх не буде, хоча в serial-моніторі вони є (етап 7 плану).
//
// Запуск команд. POST /api/console/exec виконує команду НЕ одразу, а кладе її
// в CommandQueue - той самий вхід, що для serial, MQTT і cron; виконає її
// loop(). Команда може друкувати сотні рядків, лізти на шину дисплея або
// блокувати на секунди, і в таску сервера їй не місце.
//
// Черга скінченна, тому відмова тут ЯВНА: 503 з поясненням, а не тиха втрата.
//
// Окремої "відповіді команди" не потрібно: вивід і так з'явиться у стрічці
// логу, яку вкладка вже читає.

#include <Arduino.h>
#include <ESPAsyncWebServer.h>

#include <functional>
#include <SerialCommander.hpp>
#include <TLogger.hpp>

#include "IWebModule.hpp"

// Скільки рядків максимум віддається за один GET. Не швидкодія, а розмір
// відповіді: 20 рядків по 176 байтів зібрались би в String на кілька кілобайт
// heap. Клієнт добирає решту наступним запитом - поле "next" для цього й існує.
#ifndef WEB_CONSOLE_MAX_LINES_PER_REQUEST
#define WEB_CONSOLE_MAX_LINES_PER_REQUEST 20
#endif

// Скільки кадрів SSE віддавати за один прохід loop(). Стеля навмисно низька:
// loop() крутить ще й екран, і дино, і дренаж черг.
#ifndef WEB_CONSOLE_SSE_PER_LOOP
#define WEB_CONSOLE_SSE_PER_LOOP 8
#endif

// Якщо в черзі клієнта вже стільки кадрів - пропускаємо тік. Черга росте в
// купі, і саме тут вона могла б її з'їсти.
#ifndef WEB_CONSOLE_SSE_MAX_QUEUED
#define WEB_CONSOLE_SSE_MAX_QUEUED 8
#endif

// Як часто взагалі заглядати в кільце заради SSE. loop() крутиться тисячі
// разів на секунду, а AsyncEventSource::count()/send() беруть внутрішній
// мьютекс списку клієнтів - той самий, що потрібен таску AsyncTCP, щоб
// прийняти нове з'єднання. Без цього інтервалу портал ставав недоступним, поки
// відкритий бодай один SSE-клієнт.
#ifndef WEB_CONSOLE_SSE_INTERVAL_MS
#define WEB_CONSOLE_SSE_INTERVAL_MS 100
#endif

class WebConsoleModule : public IWebModule {
public:
  static constexpr size_t kMaxLinesPerRequest = WEB_CONSOLE_MAX_LINES_PER_REQUEST;
  static constexpr size_t kSseFramesPerLoop = WEB_CONSOLE_SSE_PER_LOOP;
  static constexpr size_t kSseMaxQueued = WEB_CONSOLE_SSE_MAX_QUEUED;
  static constexpr uint32_t kSseIntervalMs = WEB_CONSOLE_SSE_INTERVAL_MS;

  // submit - CommandQueue::submit(); повертає false, якщо черга повна.
  using Submit = std::function<bool(const char* line)>;

  WebConsoleModule(SerialCommander& commander, Submit submit)
      : _commander(commander), _submit(std::move(submit)) {}

  const char* name() const override { return "console"; }
  void registerRoutes(AsyncWebServer& server, WebPortal& portal) override;

  // Віддає підписникам SSE те, що з'явилось у кільці. Кличе WebPortal::loop().
  void loop() override;

private:
  // Куди поставити курсор, коли підключився новий клієнт. Нічого не надсилає:
  // виконується в таску AsyncTCP (див. коментар у .cpp).
  void _seekForClient(AsyncEventSourceClient* client);

  SerialCommander& _commander;
  Submit _submit;
  AsyncEventSource _events{"/api/console/stream"};

  // seq наступного запису, який піде в SSE. Поки підписників немає - тримаємо
  // його на head: інакше після довгої паузи перший же клієнт отримав би повне
  // кільце застарілих рядків.
  uint32_t _cursor = 0;
  uint32_t _lastSseMs = 0;

  const TLogger _logger{"web"};
};
