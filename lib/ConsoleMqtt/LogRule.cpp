#include "LogRule.hpp"

#if defined(ESP32)

#include <cstdio>

LogRule::~LogRule() { reset(); }

void LogRule::reset() {
  if (_compiled) {
    // Без regfree() кожне перевизначення правила лишало б у купі NFA від
    // попереднього патерна - тихий тік heap, який видно лише через кілька
    // годин роботи.
    regfree(&_regex);
    _compiled = false;
  }
  _pattern.clear();
}

bool LogRule::compile(const char* pattern, char* errBuf, size_t errBufSize) {
  reset();

  if (pattern == nullptr || pattern[0] == '\0') {
    if (errBuf != nullptr && errBufSize > 0) {
      snprintf(errBuf, errBufSize, "empty pattern");
    }
    return false;
  }

  // REG_EXTENDED - ERE (потрібні '|' і '+' без екранування).
  // REG_NOSUB    - не збирати submatch: дешевший матчер і менше стека.
  // REG_NEWLINE  - '$' спрацьовує на кінці рядка, а '.' не ловить '\n'.
  const int rc = regcomp(&_regex, pattern, REG_EXTENDED | REG_NOSUB | REG_NEWLINE);
  if (rc != 0) {
    if (errBuf != nullptr && errBufSize > 0) {
      regerror(rc, &_regex, errBuf, errBufSize);
    }
    // regcomp при помилці міг уже щось виділити - звільняємо.
    regfree(&_regex);
    return false;
  }

  _pattern = pattern;
  _compiled = true;
  return true;
}

bool LogRule::matches(const char* line) const {
  if (!_compiled || line == nullptr) {
    return false;
  }
  return regexec(&_regex, line, 0, nullptr, 0) == 0;
}

#endif  // defined(ESP32)
