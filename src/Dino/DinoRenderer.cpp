#include "DinoRenderer.hpp"

#if HAS_DINO_GAME

#include "Display.h"

extern Display display;

namespace {

// Кольори сцени.
//
// Оригінал грає темно-сірим по білому, але тут свідомо чорний фон:
// (а) TFT_eSprite::fillSprite() йде через memset лише коли обидва байти
//     кольору однакові - 0x0000 і 0xFFFF так, а «сірий Chrome» 0x52AA вже ні,
//     і кожна смуга кожного кадру заливалась би повільним шляхом;
// (б) на цих платах суцільно білий екран помітно яскравіший за фон, до якого
//     звик користувач годинника.
// Саме TFT_BLACK/TFT_WHITE, а не літерал кольору: на 1-бітних панелях
// (esp8266, SSD1306) TFT_WHITE == 1, і будь-який «майже білий» літерал на
// кшталт 0x52AA став би там тим самим пікселем, що й фон.
constexpr uint16_t kBg = TFT_BLACK;
constexpr uint16_t kFg = TFT_WHITE;

constexpr uint8_t kScale = DINO_SPRITE_SCALE;

// Псевдовипадковий, але ДЕТЕРМІНОВАНИЙ від світової координати хеш.
// Саме детермінований: камінці не повинні «перестрибувати» між смугами
// одного кадру, а render() не має права смикати random().
inline uint32_t worldHash(int32_t k) { return (uint32_t)k * 2654435761u; }

}  // namespace

bool DinoRenderer::begin() {
  const int w = display.width();
  const int h = display.height();
  if (w <= 0 || h <= 0) {
    _logger.error("display is %dx%d - game disabled", w, h);
    return false;
  }

  const MonoBitmap &dino = DinoArt::trexIdle();
  const int16_t dinoW = (int16_t)(dino.width() * kScale);
  const int16_t dinoH = (int16_t)(dino.height() * kScale);

  _hudTextSize = (h >= 160) ? 2 : 1;
  _hudY = 3;
  _hudH = (int16_t)(8 * _hudTextSize);  // вбудований шрифт 5x7 у комірці 6x8
  _groundThickness = (int16_t)(kScale > 1 ? kScale : 1);

  DinoLayout L;
  L.viewW = (int16_t)w;
  L.viewH = (int16_t)h;
  // Земля не впритул до низу і не в останніх рядках екрана: при непарному
  // height()/DISPLAY_SPLIT_COUNT нижні рядки не потрапляють у жодну смугу
  // і не перемальовуються ніколи.
  L.groundY = (int16_t)(h - (_groundThickness + h / 12));
  L.playerX = (int16_t)(w / 12);
  L.playerW = dinoW;
  L.playerH = dinoH;

  // Скільки місця лишилось між HUD і маківкою діно, коли той стоїть на землі.
  const int16_t headroom = (int16_t)(L.groundY - dinoH - (_hudY + _hudH + 4));
  int16_t apex = (int16_t)(dinoH * 7 / 4);  // ~1.75 зросту, як в оригіналі
  if (apex > headroom) apex = headroom;
  if (apex < dinoH / 2) apex = (int16_t)(dinoH / 2);  // хоч якийсь стрибок
  L.jumpApex = apex;

  // Перешкоди. Великий кактус додається ЛИШЕ якщо його реально можна
  // перестрибнути - інакше на низькому екрані гра стала б непрохідною.
  const MonoBitmap &small = DinoArt::cactusSmall();
  L.obstacleW[0] = (int16_t)(small.width() * kScale);
  L.obstacleH[0] = (int16_t)(small.height() * kScale);
  L.obstacleCount = 1;

  const MonoBitmap &large = DinoArt::cactusLarge();
  const int16_t largeH = (int16_t)(large.height() * kScale);
  if (apex >= largeH + 6) {
    L.obstacleW[1] = (int16_t)(large.width() * kScale);
    L.obstacleH[1] = largeH;
    L.obstacleCount = 2;
  } else {
    _logger.info("large cactus disabled: apex %d < needed %d", (int)apex, (int)(largeH + 6));
  }

  _game.begin(L);
  _ready = _game.ready();
  if (!_ready) _logger.error("game layout rejected");
  return _ready;
}

void DinoRenderer::frame(bool frameStart) {
  if (!_ready) return;
  // Єдина точка, де рухається світ. Дивись коментар до Display::isFrameStart().
  if (frameStart) _game.update(millis());
  render();
}

void DinoRenderer::drawSprite(const MonoBitmap &bmp, int32_t x, int32_t y) {
  if (kScale == 1) {
    display.drawBitmap((int16_t)x, (int16_t)y, bmp.data(), (int16_t)bmp.width(), (int16_t)bmp.height(), kFg);
  } else {
    display.drawBitmapScaled(x, y, bmp.data(), bmp.width(), bmp.height(), kFg, kScale);
  }
}

void DinoRenderer::drawGround() {
  const DinoLayout &L = _game.layout();
  display.fillRect(0, L.groundY, L.viewW, _groundThickness, kFg);

  // Камінці. Крок 16 px у світових координатах; від них же залежить і вибір
  // спрайта, тож візерунок «їде» разом зі світом і не мерехтить.
  const int32_t step = 16 * kScale;
  const int32_t world = (int32_t)_game.scrollPx();
  const int32_t first = world / step;
  for (int32_t k = first; k * step - world < L.viewW + step; ++k) {
    const uint32_t hsh = worldHash(k);
    if ((hsh & 3u) == 0u) continue;  // ~25% пропусків, щоб не було частоколу
    const int32_t px = k * step - world;
    const int32_t py = L.groundY + _groundThickness + (int32_t)((hsh >> 8) & 1u) * _groundThickness;
    drawSprite((hsh >> 16) & 1u ? DinoArt::pebbleA() : DinoArt::pebbleB(), px, py);
  }
}

void DinoRenderer::drawObstacles() {
  const DinoLayout &L = _game.layout();
  const DinoObstacle *obs = _game.obstacles();
  for (uint8_t i = 0; i < DinoGame::maxObstacles(); ++i) {
    if (!obs[i].active) continue;
    const MonoBitmap &bmp = obs[i].kind == 0 ? DinoArt::cactusSmall() : DinoArt::cactusLarge();
    drawSprite(bmp, (int32_t)obs[i].x, L.groundY - L.obstacleH[obs[i].kind]);
  }
}

void DinoRenderer::drawPlayer() {
  const DinoLayout &L = _game.layout();
  const int32_t y = L.groundY - L.playerH - (int32_t)_game.player().y;

  const MonoBitmap *bmp = &DinoArt::trexIdle();
  switch (_game.state()) {
    case DinoState::GameOver:
      bmp = &DinoArt::trexDead();
      break;
    case DinoState::Running:
      // У повітрі ноги не перебирають - там trexIdle (обидві опущені).
      bmp = _game.player().onGround() ? (_game.runFrame() ? &DinoArt::trexRunB() : &DinoArt::trexRunA())
                                      : &DinoArt::trexIdle();
      break;
    case DinoState::Ready:
      break;
  }
  drawSprite(*bmp, L.playerX, y);
}

void DinoRenderer::drawHud() {
  char buf[24];
  snprintf(buf, sizeof(buf), "HI %05lu  %05lu", (unsigned long)_game.highScore(), (unsigned long)_game.score());

  // Шрифт 1 (вбудований 5x7) свідомо: він є на ВСІХ бекендах і всюди
  // позиціонується від верхнього лівого кута. Font 2 на C6 мапиться в
  // GFXfont, який рахується від БАЗОВОЇ ЛІНІЇ, тобто той самий код давав би
  // різну висоту HUD на різних платах.
  display.setTextFont(1);
  display.setTextSize(_hudTextSize);
  display.setTextColor(kFg);

  const int16_t x = (int16_t)(_game.layout().viewW - 4 - display.textWidth(buf));
  display.setCursor(x, _hudY);
  display.print(buf);
}

void DinoRenderer::drawOverlay() {
  const char *msg = nullptr;
  if (_game.state() == DinoState::Ready) {
    msg = "PRESS TO START";
  } else if (_game.state() == DinoState::GameOver) {
    msg = "GAME OVER";
  }
  if (!msg) return;

  const DinoLayout &L = _game.layout();
  display.setTextFont(1);
  display.setTextSize(_hudTextSize);
  display.setTextColor(kFg);
  display.setCursor((int16_t)((L.viewW - display.textWidth(msg)) / 2), (int16_t)(L.groundY / 3));
  display.print(msg);
}

void DinoRenderer::render() {
  // Тут не має бути ні millis(), ні random(), ні змін стану гри: render()
  // викликається DISPLAY_SPLIT_COUNT разів на кадр, і будь-яка залежність від
  // часу чи випадковості дала б смугам різну картинку.
  display.clear(kBg);
  drawGround();
  drawObstacles();
  drawPlayer();
  drawHud();
  drawOverlay();
}

void DinoRenderer::renderSpriteSheet() {
  display.clear(kBg);
  const MonoBitmap *all[] = {&DinoArt::trexIdle(), &DinoArt::trexRunA(),    &DinoArt::trexRunB(),
                             &DinoArt::trexDead(), &DinoArt::cactusSmall(), &DinoArt::cactusLarge(),
                             &DinoArt::cloud(),    &DinoArt::pebbleA(),     &DinoArt::pebbleB()};
  int32_t x = 2, y = 2;
  int32_t rowH = 0;
  for (const MonoBitmap *bmp : all) {
    if (x + (int32_t)bmp->width() > display.width()) {
      x = 2;
      y += rowH + 2;
      rowH = 0;
    }
    display.drawBitmap((int16_t)x, (int16_t)y, bmp->data(), (int16_t)bmp->width(), (int16_t)bmp->height(), kFg);
    x += bmp->width() + 2;
    if ((int32_t)bmp->height() > rowH) rowH = bmp->height();
  }
}

#endif  // HAS_DINO_GAME
