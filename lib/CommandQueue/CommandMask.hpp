#pragma once

// Маскування секретів у рядку команди перед тим, як його залогувати.
//
// Відлуння "> cmd" і позначка кінця "< cmd (N ms)" ідуть звичайним логом, а
// лог розходиться в кільце журналу, веб-консоль і через ConsoleMqtt у MQTT.
// Тому 'net device wifi connect X password secret' чи 'web auth user pass'
// без маски лишали б пароль у всіх цих місцях.
//
// Правило одне на всі команди, по токенах:
//   - токен після "password" або після будь-якого "...psk"
//     ('wifi-security.psk', 'wifi-sec.psk', 'psk' у 'net connection modify');
//   - четвертий токен 'web auth <user> <pass>'.
// Пробіли схлопуються в один - для логу це байдуже, а порталу так навіть
// простіше звіряти позначку кінця (assets/www/index.html, sameCommand()).

#include <Arduino.h>

// Заглушка замість секрету. Той самий рядок звіряє портал
// (assets/www/index.html, MASKED_TOKEN) - змінювати разом.
inline constexpr const char* kMaskedToken = "***";

inline String maskCommandSecrets(const char* line) {
  String out;
  if (line == nullptr) return out;

  const String src(line);
  const int length = src.length();
  bool maskNext = false;
  bool webAuth = false;
  int index = 0;
  int pos = 0;
  while (pos < length) {
    if (src[pos] == ' ') {
      ++pos;
      continue;
    }
    int end = src.indexOf(' ', pos);
    if (end < 0) end = length;
    const String token = src.substring(pos, end);
    pos = end;

    if (out.length() != 0) out += ' ';
    out += maskNext || (webAuth && index == 3) ? String(kMaskedToken) : token;

    if (index == 1) webAuth = out == "web auth";
    maskNext = token == "password" || token.endsWith("psk");
    ++index;
  }
  return out;
}
