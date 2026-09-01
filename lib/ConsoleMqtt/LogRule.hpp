#pragma once

// Одне правило фільтрації рядка логу: скомпільований POSIX-regex + вихідний
// текст патерна (щоб показати його в "console-mqtt").
//
// Чому POSIX, а не std::regex: заміряно на цьому проєкті - regcomp/regexec/
// regfree з newlib коштують 18.6 КБ тексту + 872 Б data, і вони вже лежать у
// libc.a всіх трьох тулчейнів (riscv32, xtensa-esp32, xtensa-lx106).
// std::regex - це +150...250 КБ плюс .eh_frame, а на esp32-c6 в app-розділі
// вільно ~184 КБ, тобто той env просто не зібрався б.
//
// Чому не свій wildcard-матчер (як EcoflowDeviceRegistry::wildcardMatch):
// економія 19 КБ там, де їх вистачає на всіх семи ESP32-платах, не варта
// власного коду й власних багів. Фільтр працює по ГОТОВОМУ рядку разом із
// префіксом "[I][tag    ] ", і саме на ньому виграє повний ERE:
//   ^\[[EW]\]            тільки помилки й ворнінги
//   \[(ecoflow|mqtt)     два теги одним правилом
//   took [0-9]{3,}ms     тільки те, що довше 100 мс
//
// ВАЖЛИВО: matches() виконується в КОЖНОМУ таску, що логує - включно з
// "mqtt-net" (8 КБ стека). Тому компіляція йде з REG_NOSUB: без submatch-капчу
// newlib бере дешевший матчер, а групи для рішення "так/ні" не потрібні.

#if defined(ESP32)

// sys/types.h перед regex.h обов'язковий: newlib оголошує regoff_t як off_t,
// але сам цей заголовок не підтягує - без нього збірка падає на
// "'off_t' does not name a type".
#include <sys/types.h>

#include <regex.h>

#include <string>

class LogRule {
public:
  LogRule() = default;
  ~LogRule();

  // regex_t володіє heap-буферами, які звільняє regfree() - копіювати не можна
  // (подвійний regfree), а переміщати нема потреби: правила лежать у
  // фіксованих масивах ConsoleMqtt.
  LogRule(const LogRule&) = delete;
  LogRule& operator=(const LogRule&) = delete;

  // Компілює патерн. errBuf заповнюється текстом від regerror() при невдачі -
  // логувати звідси не можна, бо правила додаються командою, а текст помилки
  // має піти у відповідь на неї.
  bool compile(const char* pattern, char* errBuf, size_t errBufSize);

  // line має бути null-terminated.
  bool matches(const char* line) const;

  void reset();

  bool valid() const { return _compiled; }
  const char* pattern() const { return _pattern.c_str(); }

private:
  std::string _pattern;
  regex_t _regex{};
  bool _compiled = false;
};

#endif  // defined(ESP32)
