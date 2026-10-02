#pragma once

// Тестова таблиця дисплея ('test-gfx'): статичні патерни для звірки панелі з
// вкладкою Screen порталу - src/TestGfx.hpp. 'test-gfx on' без патерну -
// авто-цикл кожні 5 с; 'test-gfx <pattern>' фіксує один і вимикає цикл.
// Накладок (іконка WiFi) немає: таблиця має бути чистою.

#include <SerialCommander.hpp>

#include "Screen.hpp"
#include "TestGfx.hpp"

class TestGfxScreen : public Screen {
public:
  static constexpr uint32_t kCycleMs = 5000;

  const char* name() const override { return "test-gfx"; }
  void enter() override { _cycleTs = millis(); }
  void update(bool frameStart) override;
  void drawStrip(bool frameStart) override;

  TestGfxPattern pattern() const { return _pattern; }
  bool autoCycle() const { return _autoCycle; }
  void setAutoCycle(bool on) { _autoCycle = on; }
  // Фіксує патерн і вимикає авто-цикл ("test-gfx <pattern>" - явний вибір).
  void pin(TestGfxPattern pattern);

private:
  TestGfxPattern _pattern = TestGfxPattern::Bars;
  bool _autoCycle = false;
  uint32_t _cycleTs = 0;
};

TestGfxScreen& testGfxScreen();

// Команда 'test-gfx on|off|<pattern>' - на всіх платах.
void registerTestGfxCommands(SerialCommander& commander);
