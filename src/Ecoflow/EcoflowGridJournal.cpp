#include "EcoflowGridJournal.hpp"

#include <ConfigStorage.hpp>
#include <string.h>

void EcoflowGridJournal::begin(ConfigStorage &storage, const char *key) {
  _storage = &storage;
  strncpy(_key, key, sizeof(_key) - 1);
  _key[sizeof(_key) - 1] = '\0';

  Blob blob{};
  const auto result = _storage->getStruct(_key, blob, kBlobVersion, kBlobMagic);
  if (result == ConfigStorage::StructReadResult::OK) {
    _hot = blob.hot;
  }
  // NOT_FOUND / MAGIC_MISMATCH / VERSION_MISMATCH / SIZE_MISMATCH - пристрій
  // новий, або формат змінився: лишаємо _hot за замовчуванням (Unknown,
  // усі лічильники - 0) замість довіряти даним, що не пройшли перевірку.
}

void EcoflowGridJournal::recordTransition(EcoflowGridState next) {
  if (next == EcoflowGridState::Unknown || next == _hot.currentGrid) {
    return;
  }

  const time_t now = time(nullptr);
  const bool firstEver = (_hot.currentGrid == EcoflowGridState::Unknown);

  if (firstEver) {
    // Перше визначення (за весь час життя журналу, не лише цієї сесії) -
    // нема "попереднього" стану, тривалості рахувати нема з чого.
    _hot.firstRecordEpoch = now;
  } else {
    // Тривалість стану, що ЩОЙНО завершився, зараховуємо в накопичувальний
    // лічильник "за весь час". Якщо перехід стався під час ребута (плата не
    // спостерігала якийсь час) - різниця включає й це "сліпе" вікно, звужене
    // до <=5 хв live-чекпоінтом (recordLiveCheckpoint нижче).
    const uint32_t durationSec = (_hot.gridSinceEpoch > 0 && now > _hot.gridSinceEpoch)
                                      ? static_cast<uint32_t>(now - _hot.gridSinceEpoch)
                                      : 0;
    if (_hot.currentGrid == EcoflowGridState::OnGrid) {
      _hot.totalOnGridSec += durationSec;
    } else if (_hot.currentGrid == EcoflowGridState::OffGrid) {
      _hot.totalOffGridSec += durationSec;
    }
    _hot.totalChangeCount++;
  }

  _hot.currentGrid = next;
  _hot.gridSinceEpoch = now;

  recordInternal(EcoflowJournalEntryKind::Transition, next, /*advanceHead=*/true);
}

void EcoflowGridJournal::recordBoot() {
  if (_hot.firstRecordEpoch == 0) {
    _hot.firstRecordEpoch = time(nullptr);
  }
  recordInternal(EcoflowJournalEntryKind::Boot, _hot.currentGrid, /*advanceHead=*/true);
}

void EcoflowGridJournal::recordLiveCheckpoint() {
  recordInternal(EcoflowJournalEntryKind::LiveCheckpoint, _hot.currentGrid, /*advanceHead=*/false);
}

void EcoflowGridJournal::recordInternal(EcoflowJournalEntryKind kind, EcoflowGridState toState,
                                        bool advanceHead) {
  // Слот, куди пишемо ЦЕЙ запис - до будь-яких мутацій head/count.
  const uint16_t slot = _hot.head;

  if (advanceHead) {
    _hot.head = static_cast<uint16_t>((slot + 1) % kRingCapacity);
    if (_hot.count < kRingCapacity) {
      _hot.count++;
    }
  }
  // LiveCheckpoint (advanceHead=false) перезаписує той самий слот - head і
  // count лишаються, як були, доки не станеться Transition/Boot. Це і дає
  // "не більше одного live-чекпоінта між справжніми записами".

  if (!_persistenceEnabled || _storage == nullptr) {
    return;  // "hot" уже оновлено викликачем вище - NVS лише пропускаємо
  }

  Blob blob{};
  // Результат ігнорується навмисно: NOT_FOUND (перший запис) чи
  // MISMATCH (формат змінився) - в обох випадках пишемо поверх порожнього
  // blob{} з нуля, що коректно.
  _storage->getStruct(_key, blob, kBlobVersion, kBlobMagic);

  blob.events[slot] = EcoflowGridEvent{kind, toState, time(nullptr)};
  blob.hot = _hot;

  _storage->setStruct(_key, blob, kBlobVersion, kBlobMagic);
}

size_t EcoflowGridJournal::loadRecentEvents(EcoflowGridEvent *outBuffer, size_t maxCount) const {
  if (outBuffer == nullptr || maxCount == 0 || _storage == nullptr) {
    return 0;
  }

  Blob blob{};
  const auto result = _storage->getStruct(_key, blob, kBlobVersion, kBlobMagic);
  if (result != ConfigStorage::StructReadResult::OK) {
    return 0;
  }

  const size_t available = blob.hot.count;
  const size_t toCopy = available < maxCount ? available : maxCount;
  if (toCopy == 0) {
    return 0;
  }

  // Кільце: найстаріший наявний запис - на позиції (head - count), найновіший
  // - одразу перед head. Копіюємо останні toCopy записів, у хронологічному
  // порядку (найстаріший з обраних - першим у outBuffer).
  const size_t oldestIndex = (blob.hot.head + kRingCapacity - available) % kRingCapacity;
  const size_t skip = available - toCopy;
  for (size_t i = 0; i < toCopy; i++) {
    const size_t idx = (oldestIndex + skip + i) % kRingCapacity;
    outBuffer[i] = blob.events[idx];
  }
  return toCopy;
}
