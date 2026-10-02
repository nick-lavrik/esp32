#pragma once

// Сітка спрайтів гри ('dino test') - звірка асетів на конкретній панелі.
// Окремий режим, а не разовий кадр: разовий одразу затерся б наступною
// ітерацією loop().

#include "DinoScreen.hpp"

#if HAS_DINO_GAME
class DinoSpritesScreen : public Screen {
public:
  const char* name() const override { return "dino-sprites"; }
  bool available() const override { return dinoRenderer().ready(); }
  void drawStrip(bool frameStart) override {
    (void)frameStart;
    dinoRenderer().renderSpriteSheet();
  }
};
#endif
