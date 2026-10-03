#include <cstdio>
#include <string>

#include "ButtonEvents.hpp"

static int failures = 0;
static void check(bool ok, const char* what) {
  printf("%-52s %s\n", what, ok ? "PASS" : "*** FAIL ***");
  if (!ok) failures++;
}

// Записує події рядком "press release click" - так очікуване видно одразу.
struct Recorder {
  std::string log;
  uint32_t lastHeld = 0;
  void attach(ButtonEvents& b) {
    b.onEvent([this](ButtonEvent e, uint32_t, uint32_t heldMs) {
      if (!log.empty()) log += ' ';
      log += buttonEventName(e);
      lastHeld = heldMs;
    });
  }
};

// Тримає рівень `down` протягом ms, опитуючи щомілісекунди (як cron-таск).
static uint32_t hold(ButtonEvents& b, uint32_t t, bool down, uint32_t ms) {
  for (uint32_t i = 0; i < ms; ++i) b.update(down, ++t);
  return t;
}

int main() {
  {
    ButtonEvents b;
    Recorder r;
    r.attach(b);
    uint32_t t = hold(b, 1000, false, 100);
    t = hold(b, t, true, 80);
    check(r.log == "press", "press is reported immediately");
    t = hold(b, t, false, 100);
    check(r.log == "press release", "click waits for double-click window");
    t = hold(b, t, false, 300);
    check(r.log == "press release click", "single click after window");
  }
  {
    ButtonEvents b;
    Recorder r;
    r.attach(b);
    uint32_t t = hold(b, 1000, false, 100);
    t = hold(b, t, true, 80);
    t = hold(b, t, false, 120);
    t = hold(b, t, true, 80);
    t = hold(b, t, false, 500);
    check(r.log == "press release press release double-click", "double click, no single click");
  }
  {
    ButtonEvents b;
    Recorder r;
    r.attach(b);
    uint32_t t = hold(b, 1000, false, 100);
    t = hold(b, t, true, 3100);
    check(r.log == "press long-press", "long press fires while still held");
    check(r.lastHeld == 3000, "long press reports held time");
    t = hold(b, t, false, 500);
    check(r.log == "press long-press release", "long press is not a click");
  }
  {
    // Брязкіт: 5 мс дрижання після кожного фронту не дає зайвих подій.
    ButtonEvents b;
    Recorder r;
    r.attach(b);
    uint32_t t = hold(b, 1000, false, 100);
    for (int i = 0; i < 5; ++i) t = hold(b, t, i % 2 == 0, 1);
    t = hold(b, t, true, 80);
    for (int i = 0; i < 5; ++i) t = hold(b, t, i % 2 != 0, 1);
    t = hold(b, t, false, 500);
    check(r.log == "press release click", "contact bounce filtered");
  }
  {
    // Два кліки далі за вікно - два одиночні, не подвійний.
    ButtonEvents b;
    Recorder r;
    r.attach(b);
    uint32_t t = hold(b, 1000, false, 100);
    t = hold(b, t, true, 80);
    t = hold(b, t, false, 400);
    t = hold(b, t, true, 80);
    t = hold(b, t, false, 400);
    check(r.log == "press release click press release click", "slow clicks are two singles");
  }
  {
    // Клік, а потім утримання - лише long press, без click.
    ButtonEvents b;
    Recorder r;
    r.attach(b);
    uint32_t t = hold(b, 1000, false, 100);
    t = hold(b, t, true, 80);
    t = hold(b, t, false, 100);
    t = hold(b, t, true, 3100);
    t = hold(b, t, false, 500);
    check(r.log == "press release press long-press release", "click then hold = long press only");
  }

  printf("%s (%d failed)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures);
  return failures == 0 ? 0 : 1;
}
