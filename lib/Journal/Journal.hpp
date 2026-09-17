#pragma once

// Журнал пристрою: кільце структурованих записів, з якого читають незалежні
// приймачі (serial, MQTT-дзеркало, веб-консоль, відповідь на команду).
//
// УВАГА: ВОЛАТИЛЬНИЙ. Кільце в RAM, drop-oldest, нічого не переживає ребут і
// нічого не пишеться у флеш. Слово "журнал" у багатьох викликає асоціацію з
// диском - тут її немає.
//
// Дві властивості, заради яких усе й затівалось:
//
// 1. publish() НЕ ВИКЛИКАЄ приймачів. Це один memcpy у кільце під мьютексом,
//    одиниці мікросекунд, і керування одразу повертається. Раніше кожен рядок
//    логу тягнув за собою capture, дзеркало, fanout і чергу - усе в таску
//    продюсера, тобто в "mqtt-net", "ecoflow-rest" чи в таску AsyncTCP. Звідси
//    брались і шість мьютексів на шляху рядка, і три різні латки від
//    реентерабельності. Тепер рядок, народжений усередині доставки, просто
//    лягає в кільце і піде наступною помпою - рекурсії немає за побудовою.
//
// 2. Доставка КУРСОРНА. Кожен приймач має свій seq; помпа віддає йому те,
//    чого він ще не бачив.
//
//    Політика переповнення - НА ПРИЙМАЧА, і це не дрібниця, а виправлення
//    після першого ж прогону на залізі:
//
//    - lossy (типово): відстав більше, ніж на розмір кільця - курсор
//      перестрибує на хвіст, пропуск рахується. Так і має поводитись
//      MQTT-дзеркало: без мережі його курсор стояв би вічно, і воно
//      заморозило б увесь журнал.
//    - lossless (SerialSink): publish() ПРИГАЛЬМОВУЄ продюсера, доки помпа не
//      звільнить місце. Без цього команда 'list' втрачала рядки: вона вивалює
//      ~73 рядки в тісному циклі за мікросекунди, а UART на 115200 фізично
//      віддає ~5 мс на рядок. Два прогони 'list' давали різні підмножини
//      команд. Раніше цього не траплялось, бо PrintQueue писав із таймаутом
//      прямо з log() - тобто продюсер природно чекав на UART. Тут та сама
//      протитечія, лише явна й обмежена таймаутом.
//
// Порядок глобальний: seq один на всіх, тому serial, браузер і MQTT-топік
// бачать одну послідовність.
//
// Розмір кільця - через build_flags, по платформі (див. platformio.ini):
//   -D JOURNAL_RING=48   плати з вільною RAM
//   -D JOURNAL_RING=32   C6 / st7789 / ttgo-t1
//   -D JOURNAL_RING=16   C3 (320 КБ RAM проти 512 КБ у C6, а портал+EcoFlow -
//                        обов'язкові й без дисплея-запасного шляху; кожен
//                        запис - ~180 Б, 16 замість 32 повертає heap'у ~3 КБ)
//   -D JOURNAL_RING=8    esp8266 (там вільно ~20 КБ на все)

#include <cstddef>
#include <cstdint>
#include <functional>

#include "JournalEntry.hpp"

#if defined(ESP32)
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#endif

#ifndef JOURNAL_RING
#if defined(ESP8266)
#define JOURNAL_RING 8
#else
#define JOURNAL_RING 32
#endif
#endif

// Скільки приймачів може бути підписано одночасно. Масив фіксований, без heap.
// serial + MQTT-дзеркало + до двох відповідей на команди (вкладений випадок -
// 'mailto' усередині MQTT-команди) + запас.
#ifndef JOURNAL_MAX_SINKS
#define JOURNAL_MAX_SINKS 6
#endif

// Скільки правил "рівень для тега" можна тримати одночасно. Раніше це був
// std::map<std::string, LogLevel> у LogLevelManager - на пристрої, де купа
// фрагментується, заради восьми записів, які майже ніколи не міняються.
#ifndef JOURNAL_MAX_LEVEL_RULES
#define JOURNAL_MAX_LEVEL_RULES 8
#endif

// Довжина тега в правилі. Найдовший тег у проєкті - 7 символів, запас узятий
// під ієрархічні ("mqtt.send.heartbeat" - 19).
#ifndef JOURNAL_RULE_TAG_SIZE
#define JOURNAL_RULE_TAG_SIZE 24
#endif

using JournalSubId = uint16_t;
constexpr JournalSubId kInvalidJournalSub = 0;

class Journal {
 public:
  // Повертає true, якщо запис доставлено. false означає "зараз не можу,
  // спробуй ще" - курсор не зсувається, і той самий запис прийде наступною
  // помпою. Саме так SerialSink переживає зайнятий UART, не втрачаючи рядок і
  // не ламаючи порядок.
  using Sink = std::function<bool(const JournalEntry&)>;

  static constexpr size_t kCapacity = JOURNAL_RING;
  static constexpr size_t kMaxSinks = JOURNAL_MAX_SINKS;
  static constexpr size_t kMaxLevelRules = JOURNAL_MAX_LEVEL_RULES;

  static Journal& instance();

  // Запускає помпу. На ESP32 це окремий таск: під час довгої команди
  // (sdbench, sdmap) loop() стоїть секундами, і якби помпа жила в ньому,
  // serial мовчав би весь цей час - як воно й було з PrintQueue::flush().
  // На ESP8266 RTOS немає, тому begin() нічого не робить, а pump() кличе
  // loop().
  void begin();

  // name     - для команди 'journal' (діагностика), має пережити підписку.
  // pattern  - ієрархічний тег; "" = все.
  // level    - максимальний рівень, який цікавить приймача.
  // lossless - true: продюсер чекатиме, доки цей приймач звільнить місце в
  //            кільці (див. коментар про політику вгорі). Ставити ЛИШЕ тим,
  //            хто гарантовано розгрібає: застряглий lossless-приймач
  //            гальмує весь пристрій.
  JournalSubId subscribe(const char* name, const char* pattern, LogLevel level, Sink sink,
                         bool lossless = false);
  // Знімає підписку і ГАРАНТУЄ, що після повернення приймача вже ніхто не
  // викликає: чекає, поки завершиться поточний прохід помпи. Без цього
  // CommandResponse, який живе на стеку, міг би зникнути просто посеред
  // доставки.
  //
  // НЕ КЛИКАТИ З ПРИЙМАЧА: помпа тримає той самий шлюз, і це дедлок.
  void unsubscribe(JournalSubId id);

  // Неблокуюче. text - без префікса і без '\n'.
  void publish(LogLevel level, const char* tag, const char* text, size_t length);

  // Віддає приймачам усе нове. Безпечно кликати з кількох місць: другий
  // одночасний виклик просто виходить.
  void pump();

  // Крутить помпу, доки всі приймачі не наздоженуть head або не вийде час.
  // Потрібно перед ESP.restart(): інакше останні рядки лишились би в кільці.
  void flushBlocking(uint32_t timeoutMs);

  // Копіює запис із номером seq. false - його вже (або ще) немає в кільці.
  //
  // Це і є "вид на кільце", яким живе веб-консоль після етапу 4: власного
  // буфера вона більше не тримає, а читає ті самі записи за тим самим seq.
  bool copyEntry(uint32_t seq, JournalEntry& out) const;

  uint32_t head() const { return _head; }
  uint32_t tail() const { return _head > kCapacity ? _head - kCapacity : 0; }

  // --- Рівні логування за тегом ---------------------------------------
  //
  // Переїхало сюди з LogLevelManager: та сама ієрархія через крапку ("mqtt"
  // накриває "mqtt.send"), але матчинг тепер один на проєкт - той самий
  // journalTagMatches(), яким підписуються приймачі. Раніше логіка обходу
  // ієрархії існувала у двох місцях.
  //
  // Фільтр застосовує ПРОДЮСЕР (SerialLogger::log()) до форматування - рядок,
  // який нікому не потрібен, не варто спершу склеювати, а потім викидати.

  void setDefaultLevel(LogLevel level);
  LogLevel defaultLevel() const { return _defaultLevel; }

  // false - місця під правило немає (усі kMaxLevelRules зайняті).
  bool setLevel(const char* tag, LogLevel level);
  // false - правила для цього тега й не було.
  bool clearLevel(const char* tag);
  void clearLevels();

  // Найбільш специфічне правило, що накриває тег; якщо нема - типовий рівень.
  LogLevel levelFor(const char* tag) const;

  // Чи варто взагалі формувати цей рядок.
  bool accepts(LogLevel level, const char* tag) const {
    return static_cast<int>(level) <= static_cast<int>(levelFor(tag));
  }

  struct LevelRule {
    const char* tag;
    LogLevel level;
    bool active;
  };
  LevelRule levelRuleAt(size_t index) const;

  struct SinkStats {
    const char* name;
    const char* pattern;
    LogLevel level;
    uint32_t cursor;
    uint32_t dropped;
    bool active;
    bool lossless;
  };
  SinkStats statsAt(size_t index) const;

 private:
  Journal();

  struct Slot {
    JournalSubId id = kInvalidJournalSub;
    const char* name = "";
    const char* pattern = "";
    LogLevel level = LogLevel::Verbose;
    Sink sink;
    uint32_t cursor = 0;
    uint32_t dropped = 0;
    bool lossless = false;
  };

  // Віддає одному приймачу один запис; true - курсор зсунувся.
  bool deliverOne(size_t index);

  // Скільки місця лишилось до того, як перезапис зачепить запис, який ще не
  // забрав хоч один lossless-приймач. Кличеться вже під замком.
  bool losslessFullLocked() const;

  void lock() const;
  void unlock() const;

  JournalEntry _ring[kCapacity];
  uint32_t _head = 0;  // seq наступного запису; водночас лічильник усіх

  Slot _sinks[kMaxSinks];
  JournalSubId _nextId = 1;

  struct Rule {
    char tag[JOURNAL_RULE_TAG_SIZE] = {};
    LogLevel level = LogLevel::Info;
    bool active = false;
  };
  Rule _rules[kMaxLevelRules];

#ifdef DEFAULT_LOG_LEVEL
  LogLevel _defaultLevel = static_cast<LogLevel>(DEFAULT_LOG_LEVEL);
#else
  LogLevel _defaultLevel = LogLevel::Info;
#endif

#if defined(ESP32)
  mutable SemaphoreHandle_t _mutex = nullptr;
  StaticSemaphore_t _mutexBuffer;
  SemaphoreHandle_t _pumpGate = nullptr;  // щоб помпа була одна за раз
  StaticSemaphore_t _pumpGateBuffer;
  TaskHandle_t _pumpTask = nullptr;

  static void pumpTaskEntry(void* self);
#else
  mutable bool _pumping = false;
#endif
};
