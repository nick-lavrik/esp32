#include "EcoflowJournalView.hpp"

#include <algorithm>
#include <cctype>

String ecoflowSerialFromKey(EcoflowDeviceRegistry& devices, const String& key, String* errorOut) {
  String value = key;
  value.trim();
  if (value.length() == 0) {
    if (errorOut) *errorOut = "empty device key";
    return String();
  }

  bool numeric = true;
  for (size_t i = 0; i < value.length(); i++) {
    if (!isdigit((int)value[i])) {
      numeric = false;
      break;
    }
  }

  if (numeric) {
    const size_t index = (size_t)value.toInt();
    if (index >= devices.devices().size()) {
      if (errorOut) {
        *errorOut = "index " + String((unsigned)index) + " out of range (0.." +
                    String((unsigned)(devices.devices().size() - 1)) + ")";
      }
      return String();
    }
    return String(devices.devices()[index].info->serialNumber);
  }

  for (const auto& state : devices.devices()) {
    if (value == state.info->serialNumber) {
      return value;
    }
  }
  if (errorOut) *errorOut = "unknown device: " + value;
  return String();
}

namespace {

// Дописує в out хронологічний список РЕАЛЬНИХ переходів ОДНОГО пристрою
// (найстаріший - першим, як і loadRecentEvents()).
//
// Лише Transition - Boot і LiveCheckpoint у жодному з трьох подань (текст/
// HTTP JSON/SAPI JSON) не показуються: підтвердження живості й точка
// відліку "з коли саме відомо" мають сенс лише там, де немає живої сесії,
// яка й так це підтверджує (serial/SAPI - це і є жива сесія; веб-портал
// читає той самий провайдер, той самий принцип).
//
// AGE рахується ВПЕРЕД, не назад: час від ЦІЄЇ події до НАСТУПНОГО
// Transition ТОГО САМОГО пристрою - "цей стан протримався стільки" - а не
// час від попередньої події до цієї. У розрахунку Boot/LiveCheckpoint як
// межу НЕ враховуються навіть якщо йдуть між двома Transition - дедуп у
// EcoflowGridJournal::recordTransition() вважає стан незмінним крізь Boot,
// тому й AGE має рахуватись так само (інакше мітка '>' брехала б щоразу,
// як пристрій ребутнувся без зміни grid - docs/ecoflow.md, "Журнал переходів
// grid").
void appendJournalRows(EcoflowGridJournal* journal, const char* deviceName, const char* serialNumber,
                       std::vector<EcoflowJournalRow>& out) {
  if (journal == nullptr) {
    return;
  }

  EcoflowGridEvent events[kEcoflowJournalShowLimit];
  const size_t count = journal->loadRecentEvents(events, kEcoflowJournalShowLimit);
  const time_t now = time(nullptr);
  const size_t startSize = out.size();

  for (size_t i = 0; i < count; i++) {
    if (events[i].kind != EcoflowJournalEntryKind::Transition) {
      continue;
    }

    EcoflowJournalRow row;
    row.atEpoch = events[i].atEpoch;
    row.deviceName = deviceName;
    row.serialNumber = serialNumber;
    row.toState = events[i].toState;
    out.push_back(row);
  }

  for (size_t i = startSize; i < out.size(); i++) {
    time_t until = (i + 1 < out.size()) ? out[i + 1].atEpoch : now;
    if (until <= out[i].atEpoch) until = now;
    out[i].ageSec = (until > out[i].atEpoch) ? (uint32_t)(until - out[i].atEpoch) : 0;
  }

  // Позначки в mark - лише позиційні, за цим пристроєм, тому ставляться тут
  // (усі push_back() вище - саме його рядки), а не пізніше, після
  // злиття/сортування з іншими пристроями.
  const size_t pushed = out.size() - startSize;
  if (pushed >= 1) out[out.size() - 1].mark = '>';
  if (pushed >= 2) out[out.size() - 2].mark = '<';
}

}  // namespace

bool ecoflowBuildJournalRows(EcoflowDeviceRegistry& devices, const String& target,
                              std::vector<EcoflowJournalRow>& outRows, String* errorOut) {
  outRows.clear();

  if (target.length() == 0 || target == "all") {
    for (const auto& state : devices.devices()) {
      appendJournalRows(devices.journalAt(state.journalIndex), state.info->name, state.info->serialNumber, outRows);
    }
  } else {
    const String serial = ecoflowSerialFromKey(devices, target, errorOut);
    if (serial.length() == 0) {
      return false;
    }
    EcoflowDeviceState* state = devices.find(serial);
    if (state == nullptr) {
      // Недосяжно за нормальної роботи - serial щойно підтверджений
      // ecoflowSerialFromKey() з того самого devices(). Явна відмова, а не
      // порожній результат - якщо колись розійдеться.
      if (errorOut) *errorOut = "unknown device: " + serial;
      return false;
    }
    appendJournalRows(devices.journalAt(state->journalIndex), state->info->name, state->info->serialNumber, outRows);
  }

  std::sort(outRows.begin(), outRows.end(),
            [](const EcoflowJournalRow& a, const EcoflowJournalRow& b) { return a.atEpoch < b.atEpoch; });
  // Кілька пристроїв разом можуть дати більше за ліміт одного - лишаємо лише
  // останні kEcoflowJournalShowLimit подій сумарно, найновіші.
  if (outRows.size() > kEcoflowJournalShowLimit) {
    outRows.erase(outRows.begin(), outRows.begin() + (outRows.size() - kEcoflowJournalShowLimit));
  }
  return true;
}
