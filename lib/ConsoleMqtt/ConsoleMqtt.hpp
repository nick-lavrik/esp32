#pragma once

// Дзеркало консолі в MQTT: кожен запис журналу публікується в topic (за
// замовчуванням "<prefix>/console/<MQTT_CLIENT_ID>") тим самим текстом, що в
// serial-моніторі, разом із префіксом "[I][tag    ] ".
//
// ПІДПИСНИК ЖУРНАЛУ, а не Print-приймач логера. Різниця не косметична:
//
//   - рядок приходить у таску помпи, а не в таску того, хто логував. Тому
//     зникли і re-entrancy guard (_busy), і три вбудовані deny-правила: рядок,
//     народжений усередині доставки, просто лягає в кільце й піде наступною
//     помпою, тобто обробляється РІВНО ОДИН раз. Зациклитись нема як.
//   - фільтр тепер по ТЕГУ, а не регексом по тексту. POSIX regcomp/regexec і
//     весь lib/ConsoleMqtt/LogRule видалені: тег - окреме поле запису, і
//     ієрархічний матчинг ("mqtt" накриває "mqtt.send") робить та сама
//     journalTagMatches(), що й підписка приймачів.
//
// БЕЗ БУФЕРИЗАЦІЇ. Поки MQTT не підключений (або на паузі через suspend()),
// записи для дзеркала не існують: вони нікуди не складаються й не приїдуть
// пачкою після конекту. Приймач lossy - якщо він відстав, журнал перестрибує
// його курсор уперед і рахує пропуск, але нікого не чекає.
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
// _outgoingQueue, тобто виклик із таска помпи дешевий і не блокує. На esp8266
// (єдиний env на PubSubClient) publish() пише в сокет СИНХРОННО: кожен рядок
// логу став би мережевим I/O в помпі, а помпи як окремого таска там і немає -
// вона крутиться в loop().
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
#include <Journal.hpp>
#include <TLogger.hpp>

#include <cstddef>
#include <string>

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

// Скільки тегів у КОЖНОМУ зі списків (allow / deny). Масиви фіксовані, без heap.
#ifndef CONSOLE_MQTT_MAX_RULES
#define CONSOLE_MQTT_MAX_RULES 8
#endif

class ConsoleMqtt {
public:
  static constexpr size_t kMaxRules = CONSOLE_MQTT_MAX_RULES;
  static constexpr size_t kTagSize = 24;  // як JOURNAL_RULE_TAG_SIZE
  static constexpr uint32_t kRatePerSec = CONSOLE_MQTT_RATE_PER_SEC;
  // Скільки рядків можна віддати "залпом" після паузи - щоб короткий сплеск
  // (напр. вивід "status sys") пройшов цілим, а не по краплині.
  static constexpr uint32_t kBurst = kRatePerSec * 2;

  // topic - БЕЗ префікса ("console/<client-id>"): префікс підставить сам
  // MqttClient через MqttKeyGenerator, як і для будь-якого іншого топіка.
  ConsoleMqtt(MqttClient& client, ConfigStorage& cfg, const char* topic);
  ~ConsoleMqtt();

  ConsoleMqtt(const ConsoleMqtt&) = delete;
  ConsoleMqtt& operator=(const ConsoleMqtt&) = delete;

  // Читає стан і правила з ConfigStorage, ставить фільтр ехо на СВІЙ клієнт і
  // підписується в журнал.
  //
  // Кликати ПІСЛЯ MqttClient::begin(): до нього _keyGenerator ще nullptr, і
  // топік для фільтра ехо зарезолвився б без префікса.
  void begin();

  bool active() const { return _active; }
  void setActive(bool on, bool persist);

  // deny == false - whitelist. tag - ієрархічний тег ("mqtt" накриває
  // "mqtt.send"), а не регекс. Повертає false і заповнює errBuf, якщо тег
  // задовгий або список повний.
  bool addRule(bool deny, const char* tag, char* errBuf, size_t errBufSize);
  void clearRules(bool deny);

  // Прогнати фільтр по тегу, нічого не публікуючи ("console-mqtt test <tag>").
  bool wouldPass(const char* tag) const { return passesFilters(tag); }

  void dumpStatus() const;

private:
  // Топік із префіксом - лише для показу людині (публікація резолвить його
  // сама, всередині MqttClient::publish()).
  std::string resolvedTopic() const;

  // Приймач журналу. Завжди повертає true: дзеркало не має права
  // пригальмовувати кільце (lossless - лише serial).
  bool deliver(const JournalEntry& entry);

  bool passesFilters(const char* tag) const;
  bool takeToken(uint32_t now);
  void persistRules(bool deny);
  void loadRules(bool deny, const char* key);

  MqttClient& _client;
  ConfigStorage& _cfg;
  std::string _topic;

  bool _active = (CONSOLE_MQTT_ACTIVE != 0);

  char _allow[kMaxRules][kTagSize] = {};
  char _deny[kMaxRules][kTagSize] = {};

  JournalSubId _sub = kInvalidJournalSub;

  // Token bucket у "мілі-токенах": 1000 = один дозволений рядок.
  uint32_t _tokensMilli = kBurst * 1000;
  uint32_t _lastRefillMs = 0;

  uint32_t _droppedRate = 0;
  uint32_t _published = 0;
  uint32_t _lastDropReportMs = 0;

  const TLogger _logger{"console"};
};

#endif  // HAS_CONSOLE_MQTT
