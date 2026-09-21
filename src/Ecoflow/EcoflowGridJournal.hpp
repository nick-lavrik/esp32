#pragma once

#include <Arduino.h>
#include <time.h>

#include <cstdint>

#include "EcoflowGridState.hpp"

class ConfigStorage;

// Один запис кільця переходів. Бінарне кодування - жодного тексту: текстові
// форми ("on-grid"/"off-grid", ecoflowGridStateName()) будуються з enum на
// льоту лише в момент виводу (лог/serial-команда/MQTT), у NVS і в RAM
// зберігається лише число (CLAUDE.md, "мінімальна складність коду" - тут
// про розмір, не про вкладеність, але принцип той самий: не зберігати
// похідне, що дешево порахувати заново).
enum class EcoflowJournalEntryKind : uint8_t {
  Transition = 0,       // grid реально змінився (on<->off)
  Boot = 1,              // плата щойно стартувала (setupEcoflow())
  LiveCheckpoint = 2,     // періодичне підтвердження "ще жива, стан той самий"
};

struct EcoflowGridEvent {
  EcoflowJournalEntryKind kind = EcoflowJournalEntryKind::Transition;
  EcoflowGridState toState = EcoflowGridState::Unknown;
  time_t atEpoch = 0;
};

// Персистентний журнал переходів grid для ОДНОГО EcoFlow-пристрою.
//
// "Hot" поля (нижче) живуть постійно в RAM - маленькі (див. Hot), дають
// миттєву відповідь на "який зараз стан"/"скільки всього" без звернення до
// NVS. "Cold" кільце (128 записів) НІКОЛИ не тримається постійно в RAM -
// лише транзитно, на час одного виклику recordXxx()/loadRecentEvents()
// (буфер - локальна змінна цих методів, звільняється одразу після
// використання). Причина: 128 записів x кілька пристроїв "просто так"
// з'їдали б кілька КБ постійно, хоча читаються вкрай рідко (serial-команда
// 'ecoflow-journal show', у майбутньому - веб-портал).
//
// Обидві частини живуть в ОДНОМУ NVS-блобі (ConfigStorage::setStruct/
// getStruct) під одним ключем - запис нового переходу і так вимагає
// перезапису всього блоба цілком (NVS - key-value, не кільце saмo по собі;
// ConfigStorage::setStruct/getStruct - вже наявний у проєкті механізм, що
// емулює структуру з перевіркою magic/версії поверх цього). Розділяти
// hot/cold на два ключі не дало б економії запису, лише ускладнило б
// атомарність (потрібно було б синхронізувати два незалежні NVS-записи).
//
// Контракт для майбутнього веб-порталу (НЕ реалізується зараз, але важливо
// для того, хто додаватиме /api/ecoflow/journal): користуватись ЦИМИ САМИМИ
// "hot"-акцесорами й loadRecentEvents() з ЛОКАЛЬНИМ буфером на час одного
// HTTP-запиту, так само, як serial-команда нижче. Не додавати сюди жодного
// "кешу для вебки" - інакше проблема "кілька КБ постійно в RAM" повернеться
// через чорний вхід.
class EcoflowGridJournal {
 public:
  static constexpr size_t kRingCapacity = 128;

  // key - "ecoflow.gridN" (N - індекс пристрою). Не зберігає ConfigStorage&
  // постійно як залежність понад цей виклик - лише вказівник на глобальний
  // об'єкт (main.cpp: configStorage), що живе довше за сам журнал.
  // Читає збережений "hot" стан з NVS, якщо він там є (переживає ребут).
  void begin(ConfigStorage &storage, const char *key);

  // Глобальний перемикач (serial-команда 'ecoflow-journal on|off') - вимикає
  // лише запис у NVS. "Hot"-поля в RAM оновлюються завжди (майже безкоштовно).
  void setPersistenceEnabled(bool enabled) { _persistenceEnabled = enabled; }
  bool persistenceEnabled() const { return _persistenceEnabled; }

  // --- "hot" - миттєво, без звернення до NVS ---
  EcoflowGridState currentGrid() const { return _hot.currentGrid; }
  time_t gridSinceEpoch() const { return _hot.gridSinceEpoch; }
  uint32_t totalOnGridSec() const { return _hot.totalOnGridSec; }
  uint32_t totalOffGridSec() const { return _hot.totalOffGridSec; }
  uint32_t totalChangeCount() const { return _hot.totalChangeCount; }
  time_t firstRecordEpoch() const { return _hot.firstRecordEpoch; }

  // --- записи ---
  // Дедуп - проти ПЕРСИСТЕНТНОГО currentGrid(), а не проти живого
  // EcoflowDeviceState::grid (той скидається в Unknown щоразу після ребута,
  // доки не прийде перша quota; currentGrid() тим часом одразу підвантажений
  // з NVS у begin()). Тому одна й та сама подія "перший quota після ребута
  // підтвердив той самий стан, що був до ребута" коректно НЕ записується як
  // перехід.
  void recordTransition(EcoflowGridState next);
  // Раз на фізичний старт плати (setupEcoflow()).
  void recordBoot();
  // Раз на ~5 хв (scheduler-задача) - не просуває кільце, перезаписує сам
  // себе, доки не станеться Transition/Boot.
  void recordLiveCheckpoint();

  // ОДИН getStruct(), копіює до maxCount ОСТАННІХ записів (у
  // хронологічному порядку, найстаріший - першим) у буфер ВИКЛИКАЧА.
  // Повертає, скільки реально скопійовано (<= maxCount, <= кількість
  // наявних записів).
  size_t loadRecentEvents(EcoflowGridEvent *outBuffer, size_t maxCount) const;

 private:
  struct Hot {
    EcoflowGridState currentGrid = EcoflowGridState::Unknown;
    time_t gridSinceEpoch = 0;
    uint32_t totalOnGridSec = 0;
    uint32_t totalOffGridSec = 0;
    uint32_t totalChangeCount = 0;
    time_t firstRecordEpoch = 0;
    uint16_t head = 0;   // наступний слот кільця для запису
    uint16_t count = 0;  // скільки слотів реально заповнено (<= kRingCapacity)
  };

  // Повний NVS-блоб: hot + кільце разом, один ключ, один setStruct/getStruct.
  struct Blob {
    Hot hot;
    EcoflowGridEvent events[kRingCapacity];
  };

  static constexpr uint16_t kBlobVersion = 1;
  static constexpr uint32_t kBlobMagic = 0x45434752;  // "ECGR" (EC oflow GRid)

  ConfigStorage *_storage = nullptr;
  char _key[16] = {};
  bool _persistenceEnabled = true;
  Hot _hot;  // постійно резидентна копія Blob::hot - усе, що є в RAM

  // Пише один запис у поточний слот _hot.head; advanceHead=true - завжди
  // (Transition/Boot), false - перезаписує той самий слот (LiveCheckpoint).
  // Читає-модифікує-пише повний блоб у NVS, якщо persistenceEnabled().
  void recordInternal(EcoflowJournalEntryKind kind, EcoflowGridState toState, bool advanceHead);
};
