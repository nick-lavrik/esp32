#pragma once

// Спільний "провайдер" подання журналу переходів grid (EcoflowGridJournal) -
// серійна команда 'ecoflow-journal show', HTTP-роут /api/ecoflow/journal і
// SAPI-команда 'ecoflow-journal' будують той самий список рядків ЦИМ САМИМ
// шляхом (DRY, CLAUDE.md); форматери (текст/JSON) лишаються різними для
// кожного споживача, побудова списку - спільна, одна.

#include <Arduino.h>

#include <vector>

#include "EcoflowDeviceRegistry.hpp"
#include "EcoflowGridJournal.hpp"

// На пристрій, і підсумково після злиття кількох (усі три споживачі
// показують не більше цього числа найновіших рядків).
constexpr size_t kEcoflowJournalShowLimit = 30;

// Один рядок журналу, вже готовий до подання (текст/JSON) - реальна зміна
// grid одного пристрою плюс AGE: скільки він провів у стані, яке ЦЯ подія
// позначає. Рахується тут (EcoflowGridEvent зберігає лише toState/atEpoch,
// без duration), а не в EcoflowGridJournal - це подання для виводу, не дані
// журналу.
struct EcoflowJournalRow {
  time_t atEpoch = 0;
  const char* deviceName = nullptr;
  const char* serialNumber = nullptr;
  EcoflowGridState toState = EcoflowGridState::Unknown;
  uint32_t ageSec = 0;
  // '>' - найновіший перехід пристрою (стан ще триває, AGE зростатиме з
  // кожним наступним показом); '<' - передостанній (останній ПОВНІСТЮ
  // завершений інтервал, більше не зміниться); ' ' - решта, звичайна історія.
  char mark = ' ';
};

// Розбирає серійний номер, короткий індекс у devices() чи невалідний рядок -
// той самий контракт, що serial-команди 'ecoflow-journal show'/'ecoflow-capture'
// (sn|index). Порожній результат - помилка; errorOut (якщо не nullptr) -
// людське пояснення, англійською (CLAUDE.md, "Мова виводу") - викликач сам
// вирішує, куди його подіти (лог консолі чи поле "error" у JSON-відповіді).
String ecoflowSerialFromKey(EcoflowDeviceRegistry& devices, const String& key, String* errorOut = nullptr);

// Один шлях побудови списку рядків для 'target': "all"/порожньо - усі
// пристрої, злиті в один хронологічний потік; інакше - один (sn/index).
// Сортує за atEpoch, обрізає до kEcoflowJournalShowLimit найновіших (кілька
// пристроїв разом можуть дати більше за ліміт одного). false + errorOut -
// невідомий sn/index, outRows лишається порожнім. Порожній журнал (валідний
// target, просто нема ще жодного переходу) - НЕ помилка: true + порожній
// outRows.
bool ecoflowBuildJournalRows(EcoflowDeviceRegistry& devices, const String& target,
                              std::vector<EcoflowJournalRow>& outRows, String* errorOut = nullptr);
