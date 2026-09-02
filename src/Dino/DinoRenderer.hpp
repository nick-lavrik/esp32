#pragma once

#include <DinoGame.hpp>

#include "DinoSprites.h"

// Рендер гри Chrome Dino поверх Display.
//
//   DinoRenderer dino;
//   dino.begin();                            // після display.init()
//   ...
//   dino.frame(display.isFrameStart());      // щоітерації loop(), у транзакції
//
// Розділення обов'язків: DinoGame (lib/) знає фізику й нічого не малює,
// DinoRenderer знає екран і спрайти й нічого не вирішує. Міст між ними -
// DinoLayout, який рахується тут один раз у begin().
//
// ВАЖЛИВО про смуговий рендер: render() малює ВСЮ сцену на кожній смузі
// (Display сам відсікає зайве), а фізика рухається лише коли frameStart.
class DinoRenderer {
public:
  bool begin();
  bool ready() const { return _ready; }

  DinoGame &game() { return _game; }
  const DinoGame &game() const { return _game; }

  void frame(bool frameStart);

  // Діагностика: малює всі спрайти сіткою. За один кадр видно і клипінг,
  // і масштаб, і те, чи не переплутані передній/задній план.
  void renderSpriteSheet();

private:
  void render();
  void drawGround();
  void drawObstacles();
  void drawPlayer();
  void drawHud();
  void drawOverlay();
  void drawSprite(const MonoBitmap &bmp, int32_t x, int32_t y);

  DinoGame _game;
  bool _ready = false;
  uint8_t _hudTextSize = 1;
  int16_t _hudY = 3;
  int16_t _hudH = 8;
  int16_t _groundThickness = 1;

  const TLogger _logger{"dino"};
};
