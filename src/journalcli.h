#pragma once

// Команда 'journal' - вікно в шину логу: хто читає, чи встигає, і які рівні
// пропускаються за тегом.
//
// Замінює те, чого раніше не було взагалі. Рівні існували (LogLevelManager),
// але змінити їх у рантаймі було нічим - тільки перезбіркою з іншим
// -D DEFAULT_LOG_LEVEL. Через це в проєкті завівся 'touchlog on|off': окрема
// команда, що існує рівно для того, щоб підняти один tag з debug до info
// (див. docs/architecture.md). Тепер це 'journal level touch debug'.
//
// Вивід іде через логер із тегом "journal", тобто підпадає під власний фільтр:
// після 'journal level default error' команда мовчатиме. Лікується наосліп -
// 'journal level journal info'.

#include <Arduino.h>

#include <Journal.hpp>
#include <SerialCommander.hpp>
#include <TLogger.hpp>

namespace journalcli {

static const TLogger logger{"journal"};

// Токенайзер тут навмисно свій і мінімальний: аргументи команди - це тег і
// рівень, тобто слова без пробілів. Повний розбір із лапками живе в
// src/netcli.h, і тягнути його сюди заради трьох токенів не варто (там він
// потрібен через SSID із пробілом).
static String token(const String& line, int index) {
  int start = 0;
  for (int i = 0; i <= index; ++i) {
    while (start < (int)line.length() && line[start] == ' ') ++start;
    if (i == index) break;
    while (start < (int)line.length() && line[start] != ' ') ++start;
  }
  int end = start;
  while (end < (int)line.length() && line[end] != ' ') ++end;
  return line.substring(start, end);
}

static void printSinks() {
  Journal& journal = Journal::instance();
  const uint32_t head = journal.head();

  logger.info("NAME          TAG       LEVEL    LAG  DROPPED  POLICY");
  for (size_t i = 0; i < Journal::kMaxSinks; ++i) {
    const Journal::SinkStats sink = journal.statsAt(i);
    if (!sink.active) continue;
    // LAG - скільки записів приймач ще не забрав. Ненульовий у момент виводу
    // це норма (сам цей рядок туди щойно ліг); стабільно великий - приймач не
    // встигає, і для lossy це майбутні пропуски.
    logger.info("%-12s  %-8s  %-7s  %3u  %7u  %s", sink.name,
                sink.pattern[0] != '\0' ? sink.pattern : "*",
                journalLevelFullName(sink.level), (unsigned)(head - sink.cursor),
                (unsigned)sink.dropped, sink.lossless ? "lossless" : "lossy");
  }
}

static void printLevels() {
  Journal& journal = Journal::instance();
  logger.info("default level: %s", journalLevelFullName(journal.defaultLevel()));

  size_t shown = 0;
  for (size_t i = 0; i < Journal::kMaxLevelRules; ++i) {
    const Journal::LevelRule rule = journal.levelRuleAt(i);
    if (!rule.active) continue;
    logger.info("  %-16s %s", rule.tag, journalLevelFullName(rule.level));
    ++shown;
  }
  if (shown == 0) {
    logger.info("  no per-tag rules");
  }
}

static void printStats() {
  Journal& journal = Journal::instance();
  logger.info("ring: %u entries x %u bytes = %u bytes; seq head %u, tail %u",
              (unsigned)Journal::kCapacity, (unsigned)sizeof(JournalEntry),
              (unsigned)(Journal::kCapacity * sizeof(JournalEntry)), (unsigned)journal.head(),
              (unsigned)journal.tail());
  printSinks();
}

// Останні n записів кільця. Заміна видаленій команді 'history'.
//
// ЧОМУ n ОБМЕЖЕНЕ ПОЛОВИНОЮ КІЛЬЦЯ. Ми друкуємо через логер, тобто кожен
// надрукований рядок сам лягає в те саме кільце й виштовхує найстаріший. Читаємо
// ми від seq = head-n вперед, а хвіст тим часом повзе за нами з тією ж
// швидкістю: за n надрукованих рядків він з'їдає рівно n. Поки n < kCapacity/2,
// читач іде попереду хвоста із запасом; на n = kCapacity ми зжерли б рівно те,
// що збирались показати.
static void printTail(const String& args) {
  Journal& journal = Journal::instance();

  const size_t limit = Journal::kCapacity / 2;
  size_t count = 10;
  const String arg = token(args, 1);
  if (!arg.isEmpty()) {
    const long requested = arg.toInt();
    if (requested <= 0) {
      logger.error("use: journal tail [n], n = 1..%u", (unsigned)limit);
      return;
    }
    count = (size_t)requested;
  }
  if (count > limit) {
    logger.warn("n clamped to %u: printing goes through the same ring", (unsigned)limit);
    count = limit;
  }

  const uint32_t head = journal.head();
  const uint32_t tail = journal.tail();
  uint32_t from = (head > count) ? head - (uint32_t)count : 0;
  if (from < tail) from = tail;

  char line[JournalEntry::kTextSize + 16];
  JournalEntry entry;
  for (uint32_t seq = from; seq < head; ++seq) {
    if (!journal.copyEntry(seq, entry)) continue;
    if (journalFormatLine(entry, line, sizeof(line)) == 0) continue;
    // Друкуємо як є, разом із чужим префіксом - інакше вивід не відрізнити від
    // свіжого логу. Тому %s, а не форматування наново.
    logger.info("%u %s", (unsigned)seq, line);
  }
}

static void printHelp() {
  logger.info("journal                      sinks and ring stats");
  logger.info("journal level                show default level and per-tag rules");
  logger.info("journal level <tag>          show effective level for a tag");
  logger.info("journal level <tag> <lvl>    set level; tag 'default' sets the fallback");
  logger.info("journal level <tag> off      drop the rule for a tag");
  logger.info("journal tail [n]             last n entries (n <= %u here)",
              (unsigned)(Journal::kCapacity / 2));
  logger.info("levels: error warn info debug verbose (or 0..4)");
  logger.info("tags are hierarchical: 'mqtt' also covers 'mqtt.send'");
}

static void handleLevel(const String& args) {
  Journal& journal = Journal::instance();

  const String tag = token(args, 1);
  if (tag.isEmpty()) {
    printLevels();
    return;
  }

  const String value = token(args, 2);
  const bool isDefault = tag == "default" || tag == "*";

  if (value.isEmpty()) {
    if (isDefault) {
      logger.info("default level: %s", journalLevelFullName(journal.defaultLevel()));
    } else {
      logger.info("%s -> %s", tag.c_str(), journalLevelFullName(journal.levelFor(tag.c_str())));
    }
    return;
  }

  if (!isDefault && (value == "off" || value == "clear" || value == "-")) {
    logger.info(journal.clearLevel(tag.c_str()) ? "rule for '%s' removed" : "no rule for '%s'",
                tag.c_str());
    return;
  }

  LogLevel level;
  if (!journalLevelParse(value.c_str(), level)) {
    logger.error("unknown level '%s' (error warn info debug verbose)", value.c_str());
    return;
  }

  if (isDefault) {
    journal.setDefaultLevel(level);
    logger.info("default level: %s", journalLevelFullName(level));
    return;
  }
  if (!journal.setLevel(tag.c_str(), level)) {
    logger.error("no room for another rule (max %u) or tag too long",
                 (unsigned)Journal::kMaxLevelRules);
    return;
  }
  logger.info("%s -> %s", tag.c_str(), journalLevelFullName(level));
}

static void dispatch(const String& args) {
  const String verb = token(args, 0);

  if (verb.isEmpty() || verb == "stats") {
    printStats();
  } else if (verb == "sinks") {
    printSinks();
  } else if (verb == "level") {
    handleLevel(args);
  } else if (verb == "tail") {
    printTail(args);
  } else if (verb == "help") {
    printHelp();
  } else {
    logger.error("unknown subcommand '%s' - try 'journal help'", verb.c_str());
  }
}

}  // namespace journalcli

inline void registerJournalCommand(SerialCommander& commander) {
  commander.registerCommand("journal", "log bus: sinks, stats, tail, per-tag levels (journal help)",
                            journalcli::dispatch);
  // Псевдонім: у консолі це набирається щоразу, і довге слово заважає.
  commander.registerCommand("log", "alias of 'journal'", journalcli::dispatch);
}
