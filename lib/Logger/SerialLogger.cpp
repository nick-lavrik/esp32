#include "SerialLogger.hpp"

#include <Journal.hpp>

#include <cstdio>
#include <cstring>


SerialLogger::SerialLogger(const char* tag) : ILogger(tag) {}

namespace {

// Відкочує позицію назад, поки вона стоїть на байті-ПРОДОВЖЕННІ UTF-8
// (10xxxxxx), тобто всередині багатобайтового символу.
//
// Навіщо: обрізання рядка по байту може розрізати символ навпіл. Хвіст
// втрачається, а початок лишається — і термінал показує його як U+FFFD
// ('�'). У проєкті логи українською, тому це не теоретична проблема:
// саме звідси в консолі бралися послідовності виду "������".
size_t utf8Backtrack(const char* s, size_t pos, size_t floor) {
  while (pos > floor && (static_cast<unsigned char>(s[pos]) & 0xC0) == 0x80) {
    --pos;
  }
  return pos;
}

}  // namespace

void SerialLogger::log(LogLevel level, const char* fmt, va_list args) const {
  // Фільтр рівня живе в журналі (раніше - окремий LogLevelManager): там же,
  // де матчинг тегів для приймачів, однією ієрархічною функцією на дві задачі.
  // Перевірка тут, а не в publish(), навмисно: відкинутий рядок не доходить
  // навіть до vsnprintf().
  if (!Journal::instance().accepts(level, _tag)) {
    return;
  }

  // Рядок збирається так само, як до Journal - разом із префіксом і в один
  // буфер, щоб обрізання довгих повідомлень лишилось тим самим до байта
  // (критерій приймання етапу 1, docs/journal_plan.md). У журнал іде лише
  // ТЕКСТ, без префікса: префікс додасть кожен приймач сам.
  static constexpr size_t BUF_SIZE = JournalEntry::kTextSize;
  char line[BUF_SIZE];

  const size_t prefixLen = journalFormatPrefix(level, _tag, line, sizeof(line));
  if (prefixLen == 0 || prefixLen >= sizeof(line) - 2) {
    return;  // префікс не влазить — писати нічого (не має статись)
  }

  // Місце під сам текст. Два зарезервовані байти зараз нікуди не пишуться -
  // у кільце йде лише текст, без '\n' і без термінатора. Резерв лишений
  // навмисно: він визначає, де саме обрізається довгий рядок, і прибрати його
  // означало б зсунути межу обрізання на два символи проти еталона (критерій
  // приймання етапу 1).
  const size_t avail = sizeof(line) - prefixLen - 2;
  const int msgLen = vsnprintf(line + prefixLen, avail + 1, fmt, args);
  if (msgLen < 0) {
    return;  // помилка форматування
  }

  const bool truncated = static_cast<size_t>(msgLen) > avail;
  size_t end = prefixLen + (truncated ? avail : static_cast<size_t>(msgLen));

  if (truncated) {
    // Звільнити місце під маркер "..." і відкотитись до межі символу, щоб
    // не лишити обірваний UTF-8 перед маркером.
    static constexpr size_t kMarkLen = 3;
    size_t cut = (end >= prefixLen + kMarkLen) ? end - kMarkLen : prefixLen;
    cut = utf8Backtrack(line, cut, prefixLen);
    memcpy(line + cut, "...", kMarkLen);
    end = cut + kMarkLen;
  }

  // Далі - ЛИШЕ публікація в кільце: один memcpy під мьютексом. Ні Serial, ні
  // дзеркала, ні захоплення виводу команди звідси більше не викликаються -
  // кожен із них став приймачем журналу (етапи 1, 3 і 5). Саме тому логування
  // з "mqtt-net" чи з таска AsyncTCP більше нічого не чекає.
  Journal::instance().publish(level, _tag, line + prefixLen, end - prefixLen);
}
