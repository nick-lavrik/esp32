#pragma once

// Черга команд: один вхід для всіх джерел і один виконавець у loop().
//
// НАВІЩО ОКРЕМО ВІД ЖУРНАЛУ. Лог і команда - різні за формою речі, і зшивати
// їх в одну абстракцію не можна (див. §2.6 docs/journal_plan.md). Лог
// широкомовний, читачів багато, втрата прийнятна. Команда - точка-точка, один
// виконавець, і втрата НЕПРИЙНЯТНА. Тому кільце журналу лоссі, а тут -
// гарантія: **прийнято → буде виконано**. Черга скінченна, і при переповненні
// submit() повертає false СИНХРОННО, а джерело саме вирішує, що сказати:
// serial друкує рядок, MQTT відповідає в reply-топік, веб віддає 503. Тихого
// дропу немає ніде.
//
// ЩО ЦЕ ПРИБРАЛО. До етапу 6 три входи виконували команди трьома різними
// шляхами:
//   - serial: SerialCommander::update() виконував рядок прямо на місці;
//   - MQTT: листенер викликав команду ВСЕРЕДИНІ mqtt.loop(), тобто 'sdbench'
//     на 30 с зупиняв мережевий цикл разом із keepalive;
//   - веб: через WebJobQueue, механізм, зроблений для WiFi-скану й запису в
//     NVS.
// Тепер джерела лише кладуть рядок сюди, а виконує його loop(), по одній
// команді за ітерацію.
//
// ВКЛАДЕНИЙ ВИПАДОК ('mailto <addr> <command>') лишається синхронним: команда
// всередині команди мусить виконатись у тому ж виклику, інакше зовнішня
// відповідь (лист) не побачила б виводу вкладеної. Для цього є runNow() - той
// самий код, що й у runNext(), лише без черги.

#include <Arduino.h>

#include <functional>
#include <memory>

#include <CommandResponse.hpp>
#include <ResponseTarget.hpp>
#include <TLogger.hpp>

#include "JsonApiEntry.hpp"

#if defined(ESP32)
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#endif

// Скільки команд може чекати. Спільний ресурс для ВСІХ джерел одночасно
// (serial, MQTT SAPI, веб-портал, cron) - не окремий ліміт на кожне.
// Дефолт нижче - лише запасний варіант; кожна плата задає своє значення
// явно в src-<env>/environment.h (розділ 3, "Системні піни та
// налаштування"), під свій запас RAM. Фактичний троттлінг SAPI - на боці
// браузера (sapi/index.html, SAPI_MAX_INFLIGHT=2), не тут; це число
// свідомо тримають БІЛЬШИМ за нього там, де RAM дозволяє - інакше SAPI
// сам вичерпував би всю чергу, і будь-яка друга команда з іншого джерела
// (serial/cron/портал) отримувала б busy навіть за коректної поведінки
// браузера. Точні значення по платах і чому на найслабших з них ємність
// усе ж дорівнює браузерному ліміту - docs/mqtt-web-handoff.md, "SAPI:
// обмеження одночасних запитів".
#ifndef COMMAND_QUEUE_SLOTS
#define COMMAND_QUEUE_SLOTS 4
#endif

// Максимальна довжина рядка команди. Та сама межа, що в
// SerialCommander::maxLineLength_.
#ifndef COMMAND_QUEUE_LINE
#define COMMAND_QUEUE_LINE 128
#endif

class CommandQueue {
public:
  // Що робити з рядком. У проєкті це SerialCommander::execute().
  using Executor = std::function<void(const char*)>;

  static constexpr size_t kSlots = COMMAND_QUEUE_SLOTS;
  static constexpr size_t kLineSize = COMMAND_QUEUE_LINE;

  CommandQueue();

  CommandQueue(const CommandQueue&) = delete;
  CommandQueue& operator=(const CommandQueue&) = delete;

  void begin(Executor executor) { _executor = std::move(executor); }

  // false - черга повна або рядок задовгий. Джерело ЗОБОВ'ЯЗАНЕ сказати про це
  // вголос: мовчазна відмова - це саме те, чого тут не має бути.
  // reply == nullptr - вивід іде лише в лог (serial, веб-консоль).
  bool submit(const char* line, std::shared_ptr<ResponseTarget> reply = {});

  // JSON-команда MQTT SAPI-каналу (docs/mqtt-web-handoff.md, фаза 1). entry -
  // запис реєстру (уже знайдений і провалідований викликачем - resolve()
  // відпрацював ДО цього виклику), requestId - ехо у відповіді, args/argsSize
  // - сирі байти Args-структури з того самого resolve(). Той самий контракт
  // відмови, що й у submit(): false - черга повна, викликач публікує
  // "busy" сам, повз чергу.
  bool submitJson(const JsonApiEntry* entry, uint32_t requestId, const uint8_t* args, size_t argsSize,
                   std::shared_ptr<ResponseTarget> reply);

  // Виконує НЕ БІЛЬШЕ однієї команди. Кликати з loop(). true - щось виконали.
  bool runNext();

  // Виконує рядок ПРЯМО ЗАРАЗ, повз чергу. Для вкладених команд ('mailto') і
  // для внутрішніх викликів, яким потрібен синхронний вивід.
  void runNow(const char* line, std::shared_ptr<ResponseTarget> reply = {});

  size_t pending() const;
  uint32_t rejected() const { return _rejected; }

private:
  enum class Kind : uint8_t { kText, kJson };

  struct Slot {
    Kind kind = Kind::kText;
    // kText - рядок команди; kJson - сирі байти Args-структури конкретної
    // команди (memcpy з resolve()). Один буфер на обидва кейси: розмір
    // Args сьогодні і в осяжному майбутньому - частка kLineSize.
    char payload[kLineSize] = {};
    std::shared_ptr<ResponseTarget> reply;
    // kJson-специфічні поля, ігноруються для kText. Кілька зайвих байтів на
    // джерела, які kJson не використовують, - прийнятна ціна уніфікованої
    // черги (docs/mqtt-web-handoff.md, "Рекомендована архітектура - фінальна").
    const JsonApiEntry* jsonEntry = nullptr;
    uint32_t jsonRequestId = 0;
  };

  void lock() const;
  void unlock() const;

  // kJson-гілка runNext(): один атомарний deliver(), повз Journal/
  // CommandResponse (docs/mqtt-web-handoff.md, "Ключова технічна знахідка").
  void runJsonNow(const Slot& slot);

  Executor _executor;

  Slot _slots[kSlots];
  size_t _head = 0;  // куди класти
  size_t _tail = 0;  // звідки брати
  size_t _count = 0;
  uint32_t _rejected = 0;

#if defined(ESP32)
  // submit() кличуть із таска AsyncTCP (веб) і з loop() (serial, MQTT, cron).
  mutable SemaphoreHandle_t _mutex = nullptr;
  StaticSemaphore_t _mutexBuffer;
#endif

  const TLogger _logger{"cmd.reply"};

  // Позначка кінця команди йде під тегом виконавця, а не відповіді: вона
  // стосується КОЖНОЇ команди, у тому числі тих, у яких reply немає.
  const TLogger _doneLogger{"cmd"};
};
