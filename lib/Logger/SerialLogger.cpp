#include "SerialLogger.hpp"

#include <cstdio>
#include <cstring>

#include "LogCaptureRegistry.hpp"
#include "LogLevelManager.hpp"
#include "PrintQueueRegistry.hpp"

#if SCREEN_LOG_TAIL_LINES > 0
#include "PrintFanout.hpp"
#include "ScreenLogTail.hpp"

namespace {
// Meyer's singleton: static локальна змінна гарантовано ініціалізується
// при першому виклику (уникає static initialization order fiasco між
// цим TU і ScreenLogTail.cpp/іншими глобальними об'єктами). Один спільний
// fanout (Serial + tail) для всіх SerialLogger-інстансів без явного output.
PrintFanout<2>& serialLoggerFanout() {
  static PrintFanout<2> fanout{Serial, screenLogTail()};
  return fanout;
}
}  // namespace

Print& serialLoggerOutput() { return serialLoggerFanout(); }

#else

Print& serialLoggerOutput() { return Serial; }

#endif

SerialLogger::SerialLogger(const char* tag, Print& output) : ILogger(tag), _output(output) {}

const char* SerialLogger::levelName(LogLevel level) {
  switch (level) {
    case LogLevel::Error:
      return "E";
    case LogLevel::Warn:
      return "W";
    case LogLevel::Info:
      return "I";
    case LogLevel::Debug:
      return "D";
    case LogLevel::Verbose:
      return "V";
    default:
      return "?";
  }
}

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
  if (level > LogLevelManager::instance().getLevel(_tag)) {
    return;
  }

  static constexpr size_t BUF_SIZE = PrintQueue::kLineSize;
  char line[BUF_SIZE];

  // Префікс і повідомлення формуються в ОДИН буфер.
  //
  // Раніше буферів було два по BUF_SIZE: спершу vsnprintf() у buf, потім
  // snprintf("[%s][%-5s] %s\n") у line. Через це довгий текст різався ДВІЧІ,
  // причому друге обрізання враховувало ще й префікс — тобто фактичний ліміт
  // був не BUF_SIZE, а BUF_SIZE мінус довжина префікса, і друге обрізання
  // затирало кінець першого. Обидва рази — по сирому байту, без огляду на
  // межі UTF-8.
  const int prefixLen = snprintf(line, sizeof(line), "[%s][%-7s] ", levelName(level), _tag);
  if (prefixLen < 0 || static_cast<size_t>(prefixLen) >= sizeof(line) - 2) {
    return;  // префікс не влазить — писати нічого (не має статись)
  }

  // Місце під сам текст: лишаємо 2 байти на '\n' і '\0'.
  const size_t avail = sizeof(line) - static_cast<size_t>(prefixLen) - 2;
  const int msgLen = vsnprintf(line + prefixLen, avail + 1, fmt, args);
  if (msgLen < 0) {
    return;  // помилка форматування
  }

  const bool truncated = static_cast<size_t>(msgLen) > avail;
  size_t end = static_cast<size_t>(prefixLen) + (truncated ? avail : static_cast<size_t>(msgLen));

  if (truncated) {
    // Звільнити місце під маркер "..." і відкотитись до межі символу, щоб
    // не лишити обірваний UTF-8 перед маркером.
    static constexpr size_t kMarkLen = 3;
    size_t cut = (end >= static_cast<size_t>(prefixLen) + kMarkLen) ? end - kMarkLen : static_cast<size_t>(prefixLen);
    cut = utf8Backtrack(line, cut, static_cast<size_t>(prefixLen));
    memcpy(line + cut, "...", kMarkLen);
    end = cut + kMarkLen;
  }

  line[end] = '\n';
  line[end + 1] = '\0';

  // Якщо в цьому таску активний ScopedLogCapture - той самий рядок іде ще й
  // туди (напр. у відповідь на MQTT-команду). Дублювання, а не перенаправлення:
  // консоль лишається повною. Робимо ДО черги, бо PrintQueue може відкласти
  // запис, а порядок рядків у відповіді має відповідати порядку виклику log().
  if (Print* capture = LogCaptureRegistry::instance().current()) {
    capture->write(reinterpret_cast<const uint8_t*>(line), end + 1);  // з '\n', без '\0'
  }

  // Пряме write з таймаутом 10мс; якщо _output зайнятий - рядок піде
  // в per-output чергу (PrintQueueRegistry) і буде відправлений пізніше
  // наступним log()-викликом або періодичним PrintQueue::flush().
  PrintQueueRegistry::instance().forOutput(_output).tryWrite(line, /*timeoutMs=*/10);
}
