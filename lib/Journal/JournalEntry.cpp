#include "JournalEntry.hpp"

#include <cstdio>
#include <cstring>

namespace {

// Одна таблиця на три операції: показати літеру, показати повне ім'я,
// розібрати введене користувачем. Три окремі switch'і розійшлись би - саме так
// у проєкті вже розійшлись дві копії таблиці WiFi-шифрувань (див. DRY у
// CLAUDE.md).
struct LevelInfo {
  const char* letter;
  const char* name;
};

constexpr LevelInfo kLevels[] = {
    {"E", "error"}, {"W", "warn"}, {"I", "info"}, {"D", "debug"}, {"V", "verbose"},
};
constexpr int kLevelCount = static_cast<int>(sizeof(kLevels) / sizeof(kLevels[0]));

bool equalsIgnoreCase(const char* a, const char* b) {
  for (; *a != '\0' && *b != '\0'; ++a, ++b) {
    const char ca = (*a >= 'A' && *a <= 'Z') ? static_cast<char>(*a + 32) : *a;
    const char cb = (*b >= 'A' && *b <= 'Z') ? static_cast<char>(*b + 32) : *b;
    if (ca != cb) return false;
  }
  return *a == *b;
}

}  // namespace

const char* journalLevelName(LogLevel level) {
  const int index = static_cast<int>(level);
  return (index >= 0 && index < kLevelCount) ? kLevels[index].letter : "?";
}

const char* journalLevelFullName(LogLevel level) {
  const int index = static_cast<int>(level);
  return (index >= 0 && index < kLevelCount) ? kLevels[index].name : "?";
}

bool journalLevelParse(const char* text, LogLevel& out) {
  if (text == nullptr || text[0] == '\0') return false;

  // Цифра 0..4 - той самий формат, що в -D DEFAULT_LOG_LEVEL.
  if (text[1] == '\0' && text[0] >= '0' && text[0] < '0' + kLevelCount) {
    out = static_cast<LogLevel>(text[0] - '0');
    return true;
  }

  for (int i = 0; i < kLevelCount; ++i) {
    if (equalsIgnoreCase(text, kLevels[i].name) || equalsIgnoreCase(text, kLevels[i].letter)) {
      out = static_cast<LogLevel>(i);
      return true;
    }
  }
  return false;
}

size_t journalFormatPrefix(LogLevel level, const char* tag, char* buf, size_t size) {
  const int written = snprintf(buf, size, "[%s][%-7s] ", journalLevelName(level), tag != nullptr ? tag : "");
  if (written < 0 || static_cast<size_t>(written) >= size) {
    return 0;
  }
  return static_cast<size_t>(written);
}

size_t journalFormatLine(const JournalEntry& entry, char* buf, size_t size) {
  const size_t prefixLen = journalFormatPrefix(entry.level, entry.tag, buf, size);
  if (prefixLen == 0) return 0;

  size_t length = entry.length;
  if (prefixLen + length + 1 > size) {
    length = size - prefixLen - 1;
  }
  memcpy(buf + prefixLen, entry.text, length);
  buf[prefixLen + length] = '\0';
  return prefixLen + length;
}
