#include <cstdio>
#include <cmath>
#include "DinoGame.hpp"

static int failures = 0;
static void check(bool ok, const char* what) {
  printf("%-52s %s\n", what, ok ? "PASS" : "*** FAIL ***");
  if (!ok) failures++;
}

// Крутить гру n мілісекунд кроками по step, повертає час
static uint32_t run(DinoGame& g, uint32_t t, uint32_t ms, uint32_t step = 16) {
  for (uint32_t i = 0; i < ms; i += step) { t += step; g.update(t); }
  return t;
}

int main() {
  DinoLayout L;
  L.viewW = 320; L.viewH = 240; L.groundY = 218;
  L.playerX = 26; L.playerW = 44; L.playerH = 47;
  L.obstacleW[0] = 17; L.obstacleH[0] = 35;
  L.obstacleW[1] = 25; L.obstacleH[1] = 50;
  L.obstacleCount = 2; L.jumpApex = 82;

  DinoGame g;
  g.begin(L);
  check(g.ready(), "layout accepted");
  check(g.state() == DinoState::Ready, "starts in Ready");

  uint32_t t = 1000;
  g.update(t);                       // перший кадр лише засікає час
  t = run(g, t, 500);
  check(g.state() == DinoState::Ready, "stays Ready without input");
  check(g.score() == 0, "no score before start");

  // --- старт і стрибок ---
  g.pressJump(t); t += 16; g.update(t);
  check(g.state() == DinoState::Running, "press starts the run");
  check(g.player().y > 0.0f, "first press also jumps");

  float peak = 0.0f;
  uint32_t t2 = t;
  for (int i = 0; i < 60; ++i) { t2 += 16; g.update(t2); if (g.player().y > peak) peak = g.player().y; }
  check(peak > L.jumpApex * 0.5f, "jump reaches a sane height");
  check(peak <= L.jumpApex * 1.25f, "jump does not overshoot apex");
  t = t2;

  // приземлення
  t = run(g, t, 2000);
  check(g.player().onGround(), "lands back on the ground");

  // --- короткий проти довгого натискання ---
  DinoGame a, b;
  a.begin(L); b.begin(L);
  uint32_t ta = 1000, tb = 1000;
  a.update(ta); b.update(tb);
  a.pressJump(ta); b.pressJump(tb);
  ta += 16; a.update(ta); tb += 16; b.update(tb);
  a.releaseJump(ta);                       // короткий тап
  float pa = 0, pb = 0;
  for (int i = 0; i < 60; ++i) {
    ta += 16; a.update(ta); if (a.player().y > pa) pa = a.player().y;
    tb += 16; b.update(tb); if (b.player().y > pb) pb = b.player().y;   // тримає
  }
  printf("   short tap peak = %.1f, held peak = %.1f\n", pa, pb);
  check(pb > pa * 1.15f, "holding jumps noticeably higher than a tap");
  // Навіть найкоротший тап мусить перестрибувати НАЙНИЖЧУ перешкоду,
  // інакше гравець робить усе правильно і все одно гине.
  check(pa > (float)L.obstacleH[0], "shortest tap still clears the small cactus");

  // --- рахунок росте, швидкість росте ---
  DinoGame c; c.begin(L);
  uint32_t tc = 1000; c.update(tc);
  c.pressJump(tc); tc += 16; c.update(tc);
  float v0 = c.speed();
  tc = run(c, tc, 5000);
  check(c.score() > 0, "score grows while running");
  check(c.speed() > v0, "speed grows over time");

  // --- перешкоди з'являються ---
  int active = 0;
  for (uint8_t i = 0; i < DinoGame::maxObstacles(); ++i) if (c.obstacles()[i].active) active++;
  check(active > 0, "obstacles spawn");

  // --- велика пауза не телепортує світ (clamp dt) ---
  DinoGame d; d.begin(L);
  uint32_t td = 1000; d.update(td);
  d.pressJump(td); td += 16; d.update(td);
  float before = d.scrollPx();
  td += 5000;                        // «завис» на 5 секунд
  d.update(td);
  float moved = d.scrollPx() - before;
  printf("   world moved %.1f px after a 5000 ms stall\n", moved);
  check(moved < L.viewW * 0.25f, "a long stall does not teleport the world");

  // --- game over при зіткненні ---
  DinoGame e; e.begin(L);
  uint32_t te = 1000; e.update(te);
  e.pressJump(te); te += 16; e.update(te);
  // Крутимо ДО ПЕРШОГО game over і одразу зупиняємось: якщо бігти далі,
  // блокування рестарту встигне минути і наступні перевірки втратять сенс.
  for (int i = 0; i < 4000 && e.state() == DinoState::Running; ++i) {
    te += 16; e.update(te);
  }
  check(e.state() == DinoState::GameOver, "collision ends the run");
  check(e.highScore() > 0, "high score recorded");
  check(e.highScoreDirty(), "high score marked dirty for NVS");

  // --- рестарт заблокований одразу після game over ---
  e.pressJump(te); te += 16; e.update(te);
  check(e.state() == DinoState::GameOver, "restart locked right after game over");
  te += 600; e.update(te);
  e.pressJump(te); te += 16; e.update(te);
  check(e.state() == DinoState::Running, "restart allowed after the lock expires");
  check(e.score() == 0, "score resets on restart");

  printf("\n%s (%d failed)\n", failures ? "FAILURES" : "ALL PASSED", failures);
  return failures ? 1 : 0;
}
