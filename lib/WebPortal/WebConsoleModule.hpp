#pragma once

// Розділ "console": потік логу в браузер.
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
// Команд тут НЕМАЄ - ні переліку, ні запуску. І те, і те живе в
// WebCommandsModule (POST /api/commands/exec), куди поле вводу консолі шле
// свій рядок. Два маршрути "виконати рядок" розійшлись би на першій же зміні
// валідації; вивід усе одно приходить сюди, у стрічку логу.

#include <Arduino.h>
#include <ESPAsyncWebServer.h>

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

  const char* name() const override { return "console"; }
  void registerRoutes(AsyncWebServer& server, WebPortal& portal) override;

  // Віддає підписникам SSE те, що з'явилось у кільці. Кличе WebPortal::loop().
  void loop() override;

private:
  // Куди поставити курсор, коли підключився новий клієнт. Нічого не надсилає:
  // виконується в таску AsyncTCP (див. коментар у .cpp).
  void _seekForClient(AsyncEventSourceClient* client);

  AsyncEventSource _events{"/api/console/stream"};

  // seq наступного запису, який піде в SSE. Поки підписників немає - тримаємо
  // його на head: інакше після довгої паузи перший же клієнт отримав би повне
  // кільце застарілих рядків.
  uint32_t _cursor = 0;
  uint32_t _lastSseMs = 0;
};
