#pragma once

// Прямокутник для перевірки зіткнень (AABB).
//
//   DinoRect a{10, 20, 44, 47};
//   if (a.shrunk(0.16f).intersects(b)) { /* влучили */ }
//
// Координати - float-пікселі поточного екрана (див. коментар про одиниці
// в DinoGame.hpp).
struct DinoRect {
  float x = 0.0f;
  float y = 0.0f;
  float w = 0.0f;
  float h = 0.0f;

  bool intersects(const DinoRect &o) const { return x < o.x + o.w && o.x < x + w && y < o.y + o.h && o.y < y + h; }

  // Зрізає частку k з КОЖНОГО боку. Потрібно для чесної гри: спрайт діно -
  // це габаритний прямокутник, у якому реального тіла відсотків 70, і без
  // усадки гравець "вмирав" би від дотику до порожнечі над спиною.
  DinoRect shrunk(float k) const {
    const float dx = w * k;
    const float dy = h * k;
    return DinoRect{x + dx, y + dy, w - 2.0f * dx, h - 2.0f * dy};
  }
};
