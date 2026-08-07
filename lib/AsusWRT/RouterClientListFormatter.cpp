#include "RouterClientListFormatter.hpp"

#include <cstring>

bool RouterClientListFormatter::format(const std::vector<RouterClientInfo>& clients, char* buffer,
                                        size_t bufferSize, size_t& outWrittenLength) {
  if (buffer == nullptr || bufferSize == 0) {
    outWrittenLength = 0;
    return false;
  }

  static const char* kHeaders[kColumnCount] = {"name", "ip", "mac", "type", "vendor", "timer"};

  // Будуємо тимчасові рядки-стовпці в String (динамічна пам'ять, як і решта проєкту
  // для не-ISR/не-hot-path коду) — фінальний результат копіюється у buffer лише в кінці,
  // щоб не чіпати buffer при помилці/переповненні.
  size_t rowCount = clients.size() + 1;  // +1 для заголовка
  std::vector<String> columns[kColumnCount];
  for (int c = 0; c < kColumnCount; ++c) columns[c].reserve(rowCount);

  columns[0].push_back(kHeaders[0]);
  columns[1].push_back(kHeaders[1]);
  columns[2].push_back(kHeaders[2]);
  columns[3].push_back(kHeaders[3]);
  columns[4].push_back(kHeaders[4]);
  columns[5].push_back(kHeaders[5]);

  for (const RouterClientInfo& c : clients) {
    columns[0].push_back(c.name);
    columns[1].push_back(c.ip);
    columns[2].push_back(c.mac);
    columns[3].push_back(c.type);
    columns[4].push_back(c.vendor);
    columns[5].push_back(c.timer);
  }

  // Ширина кожної колонки = довжина найдовшого значення в ній.
  size_t widths[kColumnCount];
  for (int c = 0; c < kColumnCount; ++c) {
    size_t maxLen = 0;
    for (const String& v : columns[c]) {
      if (v.length() > maxLen) maxLen = v.length();
    }
    widths[c] = maxLen;
  }

  // Рахуємо необхідний розмір заздалегідь, щоб не писати частково при переповненні.
  size_t needed = 0;
  for (size_t r = 0; r < rowCount; ++r) {
    for (int c = 0; c < kColumnCount; ++c) {
      needed += widths[c];
      if (c < kColumnCount - 1) needed += 1;  // роздільник — один пробіл
    }
    needed += 1;  // '\n'
  }
  needed += 1;  // null-термінатор

  if (needed > bufferSize) {
    outWrittenLength = needed;
    return false;
  }

  size_t pos = 0;
  for (size_t r = 0; r < rowCount; ++r) {
    for (int c = 0; c < kColumnCount; ++c) {
      const String& value = columns[c][r];
      size_t len = value.length();
      memcpy(buffer + pos, value.c_str(), len);
      pos += len;
      // Доповнення пробілами до ширини колонки (окрім останньої колонки в рядку).
      size_t pad = widths[c] - len;
      if (c < kColumnCount - 1) pad += 1;  // + роздільник
      for (size_t i = 0; i < pad; ++i) buffer[pos++] = ' ';
    }
    buffer[pos++] = '\n';
  }
  buffer[pos] = '\0';

  outWrittenLength = pos;
  return true;
}
