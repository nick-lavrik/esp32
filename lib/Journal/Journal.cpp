#include "Journal.hpp"

#include <Arduino.h>

#include <cstring>

#include "JournalTag.hpp"

// Розмір стека помпи. НЕ економити: у цьому проєкті вже обпеклись на тому, що
// врізаний стек таска дає не помилку, а мовчазне зависання.
#ifndef JOURNAL_PUMP_STACK
#define JOURNAL_PUMP_STACK 4096
#endif

// Пріоритет нижчий за loop() (той крутиться на 1): журнал не має конкурувати
// з застосунком, він лише не мусить простоювати, поки loop() заблокований.
#ifndef JOURNAL_PUMP_PRIORITY
#define JOURNAL_PUMP_PRIORITY 1
#endif

// Як довго помпа спить, якщо її ніхто не будив. Будильник - publish(), тож у
// нормі прокидання йде одразу; таймаут потрібен лише щоб добрати те, що
// приймач попереднього разу не взяв (SerialSink повернув false).
#ifndef JOURNAL_PUMP_IDLE_MS
#define JOURNAL_PUMP_IDLE_MS 20
#endif

// Скільки продюсер максимум чекає на місце в кільці, коли lossless-приймач
// відстав. Верхня межа навмисно є: якщо приймач помер, застосунок має
// сповільнитись, а не стати назавжди. Понад цей час запис таки витісняє
// найстаріший - тобто вироджуємось у lossy.
#ifndef JOURNAL_PUBLISH_WAIT_MS
#define JOURNAL_PUBLISH_WAIT_MS 50
#endif

Journal& Journal::instance() {
  // Meyer's singleton: перший виклик може статись під час C++ static-init,
  // до setup() (глобальні об'єкти з `const TLogger _logger{"tag"}`). Звичайна
  // глобальна змінна тут дала б static initialization order fiasco - та сама
  // причина, з якої нею не був serialLoggerOutput().
  static Journal journal;
  return journal;
}

#if defined(ESP32)

Journal::Journal() {
  _mutex = xSemaphoreCreateMutexStatic(&_mutexBuffer);
  _pumpGate = xSemaphoreCreateMutexStatic(&_pumpGateBuffer);
}

void Journal::lock() const { xSemaphoreTake(_mutex, portMAX_DELAY); }
void Journal::unlock() const { xSemaphoreGive(_mutex); }

void Journal::pumpTaskEntry(void* self) {
  Journal* journal = static_cast<Journal*>(self);
  for (;;) {
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(JOURNAL_PUMP_IDLE_MS));
    journal->pump();
  }
}

void Journal::begin() {
  if (_pumpTask != nullptr) return;
  xTaskCreate(pumpTaskEntry, "journal", JOURNAL_PUMP_STACK, this, JOURNAL_PUMP_PRIORITY, &_pumpTask);
}

#else

Journal::Journal() {}

// ESP8266: RTOS немає, loop() кооперативний, конкурувати нема кому - замок
// вироджується (та сама логіка, що була в PrintQueue.cpp).
void Journal::lock() const {}
void Journal::unlock() const {}

void Journal::begin() {}

#endif

bool Journal::losslessFullLocked() const {
  for (size_t i = 0; i < kMaxSinks; ++i) {
    const Slot& slot = _sinks[i];
    if (slot.id == kInvalidJournalSub || !slot.lossless) continue;
    if (_head - slot.cursor >= kCapacity) return true;
  }
  return false;
}

JournalSubId Journal::subscribe(const char* name, const char* pattern, LogLevel level, Sink sink,
                                bool lossless) {
  if (!sink) return kInvalidJournalSub;

  lock();
  JournalSubId id = kInvalidJournalSub;
  for (size_t i = 0; i < kMaxSinks; ++i) {
    if (_sinks[i].id != kInvalidJournalSub) continue;

    _sinks[i].id = _nextId++;
    _sinks[i].name = name != nullptr ? name : "";
    _sinks[i].pattern = pattern != nullptr ? pattern : "";
    _sinks[i].level = level;
    _sinks[i].sink = std::move(sink);
    _sinks[i].lossless = lossless;
    // Новий приймач починає з ХВОСТА, а не з head: те, що вже в кільці, він
    // отримає. Саме завдяки цьому рядки, залоговані до begin() (під час
    // static-init, коли Serial ще не відкритий), не губляться.
    _sinks[i].cursor = _head > kCapacity ? _head - kCapacity : 0;
    _sinks[i].dropped = 0;
    id = _sinks[i].id;
    break;
  }
  unlock();
  return id;
}

void Journal::unsubscribe(JournalSubId id) {
  if (id == kInvalidJournalSub) return;

#if defined(ESP32)
  // Спершу дочекатись помпи: вона кличе приймача БЕЗ мьютекса журналу, тож
  // самого лише замка мало - доставка могла б тривати вже після того, як слот
  // зник і об'єкт приймача помер.
  xSemaphoreTake(_pumpGate, portMAX_DELAY);
#endif

  lock();
  for (size_t i = 0; i < kMaxSinks; ++i) {
    if (_sinks[i].id == id) {
      _sinks[i] = Slot{};
      break;
    }
  }
  unlock();

#if defined(ESP32)
  xSemaphoreGive(_pumpGate);
#endif
}

void Journal::publish(LogLevel level, const char* tag, const char* text, size_t length) {
  if (length >= JournalEntry::kTextSize) {
    length = JournalEntry::kTextSize - 1;
  }

  // Протитечія для lossless-приймачів. Чекаємо ПЕРЕД захопленням замка й
  // ніколи - всередині помпи (інакше приймач, який сам щось логує, заблокував
  // би сам себе).
#if defined(ESP32)
  if (_pumpTask != nullptr && xTaskGetCurrentTaskHandle() != _pumpTask) {
    const uint32_t deadline = millis() + JOURNAL_PUBLISH_WAIT_MS;
    for (;;) {
      lock();
      const bool full = losslessFullLocked();
      unlock();
      if (!full || static_cast<int32_t>(millis() - deadline) >= 0) break;
      xTaskNotifyGive(_pumpTask);
      vTaskDelay(1);
    }
  }
#else
  // ESP8266: помпа живе в loop(), тобто в тому самому потоці - чекати нема на
  // кого, треба прокрутити її самим.
  {
    lock();
    const bool full = losslessFullLocked();
    unlock();
    if (full) pump();
  }
#endif

  lock();
  JournalEntry& entry = _ring[_head % kCapacity];
  entry.seq = _head;
  entry.timeMs = millis();
  entry.tag = tag != nullptr ? tag : "";
  entry.level = level;
  entry.length = static_cast<uint16_t>(length);
  if (length > 0 && text != nullptr) {
    memcpy(entry.text, text, length);
  }
  entry.text[length] = '\0';
#if defined(ESP32)
  entry.task = pcTaskGetName(nullptr);
#else
  entry.task = "loop";
#endif
  ++_head;
  unlock();

#if defined(ESP32)
  // Будимо помпу. Якщо вона ще не створена (static-init до begin()), запис
  // просто чекає в кільці.
  if (_pumpTask != nullptr) {
    xTaskNotifyGive(_pumpTask);
  }
#endif
}

// Віддає приймачу i РІВНО ОДИН запис. true - запис пішов (курсор зсунувся).
//
// Один, а не всі. Раніше помпа вичерпувала приймача до кінця, перш ніж перейти
// до наступного. Поки вона сиділа всередині повільного приймача (MQTT-дзеркало
// кличе PicoMQTT::publish(), який може чекати на замок мережевого таска),
// курсор lossless-приймача стояв, а продюсери тим часом крутили кільце - і
// serial губив би рядки, хоч сам устигав завжди. Обхід по колу дає йому чергу
// між кожними двома публікаціями повільного.
//
// ЧЕСНО: цей шлях НЕ спостерігався на залізі. Я переписав помпу, шукаючи
// причину "dropped 12" у serial, і помилився - причина була інша (порт USB CDC
// без читача, див. §10.4 в docs/journal_plan.md). Зміна лишена, бо усуває
// реальну залежність гарантії від чужої затримки, а не тому, що щось вилікувала.
bool Journal::deliverOne(size_t index) {
  lock();
  Slot& slot = _sinks[index];
  if (slot.id == kInvalidJournalSub || slot.cursor >= _head) {
    unlock();
    return false;
  }

  const uint32_t oldest = _head > kCapacity ? _head - kCapacity : 0;
  if (slot.cursor < oldest) {
    slot.dropped += oldest - slot.cursor;  // приймач відстав - пропуск
    slot.cursor = oldest;
  }

  // Знімок під замком: слот і сам запис можуть змінитись, щойно ми відпустимо
  // мьютекс, а sink кличемо навмисно БЕЗ нього - він пише в UART чи в мережу і
  // має право там застрягти на мілісекунди.
  JournalEntry entry = _ring[slot.cursor % kCapacity];
  const bool wanted = static_cast<int>(entry.level) <= static_cast<int>(slot.level) &&
                      journalTagMatches(slot.pattern, entry.tag);
  Sink sink = wanted ? slot.sink : Sink{};
  unlock();

  if (wanted && !sink(entry)) {
    return false;  // не взяв - курсор не зсуваємо, спробуємо наступного разу
  }

  lock();
  // Слот міг зникнути (unsubscribe) або курсор поїхати вперед, поки ми були
  // поза замком - зсуваємо лише свій власний запис.
  if (_sinks[index].id != kInvalidJournalSub && _sinks[index].cursor == entry.seq) {
    _sinks[index].cursor = entry.seq + 1;
  }
  unlock();
  return true;
}

void Journal::pump() {
#if defined(ESP32)
  if (xSemaphoreTake(_pumpGate, 0) != pdTRUE) return;  // помпа вже крутиться
#else
  if (_pumping) return;
  _pumping = true;
#endif

  // Коло: по одному запису кожному приймачу за прохід. Так lossless-приймач
  // отримує чергу між кожними двома публікаціями повільного - і не залежить
  // від того, наскільки той затримався.
  for (bool progress = true; progress;) {
    progress = false;
    for (size_t i = 0; i < kMaxSinks; ++i) {
      if (deliverOne(i)) progress = true;
    }
  }

#if defined(ESP32)
  xSemaphoreGive(_pumpGate);
#else
  _pumping = false;
#endif
}

void Journal::flushBlocking(uint32_t timeoutMs) {
  const uint32_t deadline = millis() + timeoutMs;
  for (;;) {
    pump();

    lock();
    bool drained = true;
    for (size_t i = 0; i < kMaxSinks; ++i) {
      if (_sinks[i].id != kInvalidJournalSub && _sinks[i].cursor < _head) {
        drained = false;
        break;
      }
    }
    unlock();

    if (drained || static_cast<int32_t>(millis() - deadline) >= 0) return;
    delay(1);
  }
}

bool Journal::copyEntry(uint32_t seq, JournalEntry& out) const {
  lock();
  const uint32_t oldest = _head > kCapacity ? _head - kCapacity : 0;
  const bool alive = seq >= oldest && seq < _head;
  if (alive) {
    out = _ring[seq % kCapacity];
  }
  unlock();
  return alive;
}

Journal::SinkStats Journal::statsAt(size_t index) const {
  SinkStats stats{"", "", LogLevel::Verbose, 0, 0, false, false};
  if (index >= kMaxSinks) return stats;

  lock();
  const Slot& slot = _sinks[index];
  stats.active = slot.id != kInvalidJournalSub;
  stats.name = slot.name;
  stats.pattern = slot.pattern;
  stats.level = slot.level;
  stats.cursor = slot.cursor;
  stats.dropped = slot.dropped;
  stats.lossless = slot.lossless;
  unlock();
  return stats;
}

// --- Рівні логування за тегом ------------------------------------------------
//
// БЕЗ ЗАМКА, і це свідомо. levelFor() кличеться на КОЖЕН рядок логу з будь-якого
// таска, а правила міняє людина командою 'journal level' - раз на кілька
// годин. Мьютекс тут коштував би постійно, а рятував би від нічого: гонка може
// дати лише "рядок відфільтрувався за старим правилом". Активація правила -
// остання дія в setLevel(), тому читач бачить або старе правило, або вже
// повністю записане нове, але не напівскопійований тег.
//
// LogLevelManager замка теж не брав; різниця в тому, що там був std::map, і
// одночасні find()/insert() були реальним UB.

void Journal::setDefaultLevel(LogLevel level) { _defaultLevel = level; }

bool Journal::setLevel(const char* tag, LogLevel level) {
  if (tag == nullptr || tag[0] == '\0') {
    setDefaultLevel(level);
    return true;
  }
  if (strlen(tag) >= JOURNAL_RULE_TAG_SIZE) return false;

  for (size_t i = 0; i < kMaxLevelRules; ++i) {
    if (_rules[i].active && strcmp(_rules[i].tag, tag) == 0) {
      _rules[i].level = level;
      return true;
    }
  }
  for (size_t i = 0; i < kMaxLevelRules; ++i) {
    if (_rules[i].active) continue;
    strncpy(_rules[i].tag, tag, JOURNAL_RULE_TAG_SIZE - 1);
    _rules[i].tag[JOURNAL_RULE_TAG_SIZE - 1] = '\0';
    _rules[i].level = level;
    _rules[i].active = true;  // останньою дією: див. коментар вище
    return true;
  }
  return false;
}

bool Journal::clearLevel(const char* tag) {
  if (tag == nullptr) return false;
  for (size_t i = 0; i < kMaxLevelRules; ++i) {
    if (_rules[i].active && strcmp(_rules[i].tag, tag) == 0) {
      _rules[i].active = false;
      return true;
    }
  }
  return false;
}

void Journal::clearLevels() {
  for (size_t i = 0; i < kMaxLevelRules; ++i) {
    _rules[i].active = false;
  }
}

LogLevel Journal::levelFor(const char* tag) const {
  // Виграє НАЙДОВШЕ правило, що накриває тег: "mqtt.send" специфічніше за
  // "mqtt". Так само поводився LogLevelManager, лише навпаки - він відрізав
  // від тега по крапці й шукав точний ключ.
  const Rule* best = nullptr;
  size_t bestLen = 0;
  for (size_t i = 0; i < kMaxLevelRules; ++i) {
    const Rule& rule = _rules[i];
    if (!rule.active || !journalTagMatches(rule.tag, tag)) continue;
    const size_t len = strlen(rule.tag);
    if (best == nullptr || len > bestLen) {
      best = &rule;
      bestLen = len;
    }
  }
  return best != nullptr ? best->level : _defaultLevel;
}

Journal::LevelRule Journal::levelRuleAt(size_t index) const {
  if (index >= kMaxLevelRules) return LevelRule{"", LogLevel::Info, false};
  return LevelRule{_rules[index].tag, _rules[index].level, _rules[index].active};
}
