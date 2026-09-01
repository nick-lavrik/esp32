#pragma once

// Дзеркало консолі в MQTT: кожен рядок, що йде через SerialLogger, публікується
// в topic (за замовчуванням "<prefix>/console/<MQTT_CLIENT_ID>") - тим самим
// текстом, що в serial-моніторі, разом із префіксом "[I][tag    ] ".
//
// БЕЗ БУФЕРИЗАЦІЇ. Поки MQTT не підключений (або на паузі через suspend()),
// рядки просто не існують для дзеркала: вони нікуди не складаються й не
// приїдуть пачкою після конекту. Тобто в топіку видно рівно те, що відбувалось
// при живому з'єднанні.
//
// Чому це тут, а не в lib/CommandResponse. Там per-task захоплення
// (ScopedLogCapture) - воно навмисно бере рядки лише з таска, де виконується
// команда. Дзеркалу потрібно протилежне: ВСІ таски, включно з "mqtt-net" і
// "ecoflow-rest". Тому механізм інший - глобальний LogMirror, і два працюють
// одночасно (рядок може піти і у відповідь на команду, і в дзеркало).
//
// Залежності передаються в конструктор, глобалів усередині немає: дзеркало
// можна посадити на ОКРЕМИЙ MqttClient (напр. на інший брокер), не чіпаючи
// бібліотеку.

#include <MqttClient.hpp>

// HAS_CONSOLE_MQTT визначає сам заголовок - як HAS_MQTT_CLIENT у MqttClient.hpp
// і HAS_GMAIL_SENDER у GmailSender.hpp. У platformio.ini його немає; щоб
// вирізати механізм у конкретному env, туди додається -D HAS_CONSOLE_MQTT=0.
//
// Прив'язка до PicoMQTT не косметична. Там publish() лише КЛАДЕ команду в
// _outgoingQueue, тобто виклик із будь-якого таска дешевий і не блокує. На
// esp8266 (єдиний env на PubSubClient) publish() пише в сокет СИНХРОННО: кожен
// рядок логу став би мережевим I/O всередині log(), а рядок, залогований із
// callback-у самого PubSubClient, дав би реентерабельний запис у той самий
// сокет - те, що вже ламало MQTT-потік на C6 (див. networkTaskLoop()).
#if !defined(HAS_CONSOLE_MQTT)
#  if defined(ESP32) && HAS_MQTT_CLIENT && __has_include(<PicoMQTT.h>)
#    define HAS_CONSOLE_MQTT 1
#  else
#    define HAS_CONSOLE_MQTT 0
#  endif
#endif
// Платформа має право вето навіть над явним -D HAS_CONSOLE_MQTT=1.
#if HAS_CONSOLE_MQTT && (!defined(ESP32) || !HAS_MQTT_CLIENT || !__has_include(<PicoMQTT.h>))
#  undef HAS_CONSOLE_MQTT
#  define HAS_CONSOLE_MQTT 0
#endif

#if HAS_CONSOLE_MQTT

#include <Arduino.h>
#include <ConfigStorage.hpp>
#include <PrintQueue.hpp>
#include <TLogger.hpp>

#include <atomic>
#include <cstddef>
#include <string>

#include "LogRule.hpp"

// Стан дзеркала одразу після першого старту (поки не збережено в NVS).
#ifndef CONSOLE_MQTT_ACTIVE
#define CONSOLE_MQTT_ACTIVE 0
#endif

// Скільки рядків на секунду максимум іде в топік. Не оптимізація, а запобіжник:
// _outgoingQueue має 32 слоти й drop-oldest, тож нестримний потік логу витісняв
// би з неї корисні публікації (heartbeat, LWT, відповіді на команди).
#ifndef CONSOLE_MQTT_RATE_PER_SEC
#define CONSOLE_MQTT_RATE_PER_SEC 10
#endif

// Скільки правил у КОЖНОМУ зі списків (allow / deny). Масиви фіксовані, без
// heap - як PrintQueue і LogCaptureRegistry.
#ifndef CONSOLE_MQTT_MAX_RULES
#define CONSOLE_MQTT_MAX_RULES 8
#endif

class ConsoleMqtt : public Print {
public:
  static constexpr size_t kMaxRules = CONSOLE_MQTT_MAX_RULES;
  static constexpr uint32_t kRatePerSec = CONSOLE_MQTT_RATE_PER_SEC;
  // Скільки рядків можна віддати "залпом" після паузи - щоб короткий сплеск
  // (напр. вивід "status sys") пройшов цілим, а не по краплині.
  static constexpr uint32_t kBurst = kRatePerSec * 2;

  // topic - БЕЗ префікса ("console/<client-id>"): префікс підставить сам
  // MqttClient через MqttKeyGenerator, як і для будь-якого іншого топіка.
  ConsoleMqtt(MqttClient& client, ConfigStorage& cfg, const char* topic);

  // Читає стан і правила з ConfigStorage, компілює вбудовані правила, ставить
  // фільтр ехо на СВІЙ клієнт і вішає себе в LogMirror.
  //
  // Кликати ПІСЛЯ MqttClient::begin(): до нього _keyGenerator ще nullptr, і
  // топік для фільтра ехо зарезолвився б без префікса.
  void begin();

  bool active() const { return _active; }
  void setActive(bool on, bool persist);

  // deny == false - whitelist. Повертає false і заповнює errBuf, якщо патерн
  // не компілюється або список повний.
  bool addRule(bool deny, const char* pattern, char* errBuf, size_t errBufSize);
  void clearRules(bool deny);

  // Прогнати фільтр по рядку, нічого не публікуючи ("console-mqtt test").
  bool wouldPass(const char* line) const;

  void dumpStatus() const;

  // Print: сюди SerialLogger віддає готовий рядок (з '\n', null-terminated).
  size_t write(const uint8_t* buffer, size_t size) override;
  // Дзеркало приймає лише цілі рядки - побайтовий Print-інтерфейс не
  // підтримується (SerialLogger ним не користується).
  size_t write(uint8_t) override { return 1; }

private:
  // Вбудовані deny-правила: не персистяться, не видаляються, застосовуються
  // ПЕРШИМИ. Без них дзеркало годує саме себе - див. коментар у .cpp.
  static const char* const kBuiltinDeny[];
  static const size_t kBuiltinDenyCount;

  // Топік із префіксом - лише для показу людині (публікація резолвить його
  // сама, всередині MqttClient::publish()).
  std::string resolvedTopic() const;

  bool passesFilters(const char* line) const;
  bool takeToken(uint32_t now);
  void persistRules(bool deny);
  void loadRules(bool deny, const char* key);

  MqttClient& _client;
  ConfigStorage& _cfg;
  std::string _topic;

  bool _active = (CONSOLE_MQTT_ACTIVE != 0);

  LogRule _builtin[4];
  LogRule _allow[kMaxRules];
  LogRule _deny[kMaxRules];

  // Re-entrancy guard. Плоский прапорець, а НЕ per-task: publish() тут - це
  // лише enqueueOutgoing(), тобто мікросекунди, тож у гіршому разі губиться
  // кілька рядків з іншого таска, поки перший у процесі. Це строгіше за
  // per-task guard і на два порядки простіше. Без нього будь-який лог,
  // народжений усередині доставки, повертався б сюди ж.
  std::atomic_flag _busy = ATOMIC_FLAG_INIT;

  // Token bucket у "мілі-токенах": 1000 = один дозволений рядок.
  uint32_t _tokensMilli = kBurst * 1000;
  uint32_t _lastRefillMs = 0;

  uint32_t _droppedRate = 0;
  uint32_t _published = 0;
  uint32_t _lastDropReportMs = 0;

  const TLogger _logger{"console"};
};

#endif  // HAS_CONSOLE_MQTT
