#pragma once

#include <Arduino.h>

#include <vector>

#include "RouterClientInfo.hpp"

// Заміна `column -ts $` — вирівнює клієнтів у текстову таблицю
// (заголовок + рядки, колонки вирівняні пробілами по макс. ширині).
//
// Приклад використання:
//   char buf[1024];
//   size_t written = 0;
//   if (!RouterClientListFormatter::format(clients, buf, sizeof(buf), written)) {
//     Logger::error("буфер замалий, потрібно >= %u байт", written);
//   } else {
//     Serial.print(buf);
//   }
class RouterClientListFormatter {
public:
  // Форматує clients у buffer (null-terminated). bufferSize — повний розмір buffer.
  // outWrittenLength повертає фактичну довжину запису (без null-термінатора) при успіху,
  // або МІНІМАЛЬНО НЕОБХІДНИЙ розмір буфера (включно з null-термінатором) при переповненні.
  // Повертає false, якщо buffer==nullptr, bufferSize==0, або buffer замалий для результату
  // (у цьому разі buffer НЕ чіпається — часткового запису не відбувається).
  static bool format(const std::vector<RouterClientInfo>& clients, char* buffer, size_t bufferSize,
                      size_t& outWrittenLength);

private:
  static const int kColumnCount = 6;
};
