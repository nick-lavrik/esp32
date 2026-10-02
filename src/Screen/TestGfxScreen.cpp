#include "TestGfxScreen.hpp"

#include <string.h>

#include <Logger.hpp>

#include "ScreenManager.hpp"

TestGfxScreen& testGfxScreen() {
  static TestGfxScreen screen;
  return screen;
}

void TestGfxScreen::update(bool frameStart) {
  // Перемикання патерну в авто-циклі - лише на початку ПОВНОГО кадру: між
  // ітераціями loop() у межах кадру сцена мінятись не має, інакше кожна
  // смуга показала б інший патерн.
  if (!_autoCycle || !frameStart || millis() - _cycleTs < kCycleMs) return;
  _pattern = testGfxNextPattern(_pattern);
  _cycleTs = millis();
  screens.invalidate();
}

void TestGfxScreen::drawStrip(bool frameStart) {
  (void)frameStart;
  drawTestGfx(_pattern);
}

void TestGfxScreen::pin(TestGfxPattern pattern) {
  _autoCycle = false;
  _pattern = pattern;
  _cycleTs = millis();
  screens.invalidate();
}

void registerTestGfxCommands(SerialCommander& commander) {
  commander.registerCommand("test-gfx",
                            "display graphics test patterns: test-gfx on (cycles patterns every 5s) | off | "
                            "bars|gray|gradient|frame|checker|primitives (pins one pattern)",
                            [](const String& args) {
                              TestGfxScreen& tg = testGfxScreen();
                              const bool active = !strcmp(screens.active().name(), tg.name());
                              TestGfxPattern p;
                              if (args.equalsIgnoreCase("on")) {
                                tg.setAutoCycle(true);
                                screens.request(tg.name());
                                Logger::info("test-gfx ON (%s, auto-cycle 5s)", testGfxPatternName(tg.pattern()));
                              } else if (args.equalsIgnoreCase("off")) {
                                if (active) screens.requestHome();
                                Logger::info("test-gfx OFF (%s)", testGfxPatternName(tg.pattern()));
                              } else if (args.length() == 0) {
                                Logger::info("test-gfx: %s (%s%s)", active ? "ON" : "OFF",
                                             testGfxPatternName(tg.pattern()),
                                             (active && tg.autoCycle()) ? ", auto-cycle 5s" : "");
                              } else if (testGfxPatternFromName(args.c_str(), &p)) {
                                tg.pin(p);
                                screens.request(tg.name());
                                Logger::info("test-gfx ON (%s)", testGfxPatternName(p));
                              } else {
                                Logger::info("use: test-gfx on|off|bars|gray|gradient|frame|checker|primitives");
                              }
                            });
}
