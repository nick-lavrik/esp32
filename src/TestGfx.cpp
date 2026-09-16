// TestGfx.cpp
#include "TestGfx.hpp"

#include <Arduino.h>
#include <string.h>

#include "Display.h"

extern Display display;

namespace {

// Малює прямокутну сітку cols x rows, що цілком укриває [x0,x0+w) x
// [y0,y0+h): останній стовпець/рядок бере залишок від цілочисельного
// ділення, інакше на непарних розмірах екрана лишалась би неукрита смужка
// праворуч/знизу. Один цей хелпер обслуговує bars/gray/gradient/checker -
// друга копія того самого циклу під іншою назвою була б рівно тим
// дублюванням, проти якого застерігає CLAUDE.md.
void fillGrid(int y0, int h, int cols, int rows, uint16_t (*colorAt)(int col, int row)) {
  const int w = display.width();
  if (w <= 0 || h <= 0 || cols <= 0 || rows <= 0) return;

  for (int r = 0; r < rows; r++) {
    const int cy = y0 + r * h / rows;
    const int ch = (r == rows - 1) ? (y0 + h - cy) : (h / rows);
    for (int c = 0; c < cols; c++) {
      const int cx = c * w / cols;
      const int cw = (c == cols - 1) ? (w - cx) : (w / cols);
      display.fillRect(cx, cy, cw, ch, colorAt(c, r));
    }
  }
}

uint16_t gray565(uint8_t level) {
  const uint16_t r5 = level >> 3;
  const uint16_t g6 = level >> 2;
  const uint16_t b5 = level >> 3;
  return (uint16_t)((r5 << 11) | (g6 << 5) | b5);
}

// Лише RED/GREEN/YELLOW/CYAN/WHITE/BLACK/DARKGREY - на монохромному
// SSD1306-шимі (esp8266) BLUE і MAGENTA взагалі не визначені
// (include/Setup_SSD1306_NodeMCU.h), а безумовне використання тут дало б
// помилку компіляції саме на цій платі.
uint16_t barColorAt(int col, int) {
  static constexpr uint16_t kColors[] = {TFT_WHITE, TFT_YELLOW, TFT_CYAN, TFT_GREEN, TFT_RED, TFT_BLACK};
  constexpr int kCount = sizeof(kColors) / sizeof(kColors[0]);
  return kColors[col % kCount];
}

uint16_t grayRampAt(int col, int) { return gray565((uint8_t)(col * 255 / 15)); }

uint16_t grayPatchAt(int col, int) {
  static constexpr uint8_t kLevels[] = {0, 8, 128, 247, 255};
  return gray565(kLevels[col % 5]);
}

// 3 смуги по 32 кроки - чиста R, чиста G, чиста B (рахуються нарізно,
// бо в RGB565 у них різна розрядність: 5/6/5 біт).
uint16_t gradientRampAt(int col, int row) {
  switch (row) {
    case 0: return (uint16_t)((col * 31 / 31) << 11);
    case 1: return (uint16_t)((col * 63 / 31) << 5);
    default: return (uint16_t)(col * 31 / 31);
  }
}

// Повна палітра RGB332 (256 кольорів) сіткою 16x16 - та сама конвертація,
// що pushImage8bpp() використовує для фонових зображень (Display::rgb332to565).
uint16_t paletteAt(int col, int row) { return Display::rgb332to565((uint8_t)(row * 16 + col)); }

uint16_t checkerAt(int col, int row) { return ((col + row) & 1) ? TFT_WHITE : TFT_BLACK; }

void drawFrame(int w, int h) {
  display.clear(TFT_BLACK);
  display.drawRect(0, 0, w, h, TFT_WHITE);

  constexpr int kCorner = 10;
  display.fillRect(0, 0, kCorner, kCorner, TFT_RED);                       // top-left
  display.fillRect(w - kCorner, 0, kCorner, kCorner, TFT_GREEN);           // top-right
  display.fillRect(0, h - kCorner, kCorner, kCorner, TFT_YELLOW);          // bottom-left
  display.fillRect(w - kCorner, h - kCorner, kCorner, kCorner, TFT_CYAN);  // bottom-right

  display.fillRect(w / 2, 0, 1, h, TFT_DARKGREY);  // вертикаль перехрестя
  display.fillRect(0, h / 2, w, 1, TFT_DARKGREY);  // горизонталь перехрестя
}

// Хрестик 8x8, 1bpp (MSB = лівий піксель рядка) - для перевірки
// drawBitmapScaled() без залежності від ігрових спрайтів (src/Dino/),
// які існують лише за HAS_DINO_GAME.
const uint8_t kDemoBitmap[8] PROGMEM = {
    0b00011000, 0b00011000, 0b00011000, 0b11111111,
    0b11111111, 0b00011000, 0b00011000, 0b00011000,
};

void drawPrimitives(int w, int h) {
  display.clear(TFT_BLACK);
  display.drawRect(4, 4, w - 8, h - 8, TFT_WHITE);
  display.fillRect(14, 14, 20, 20, TFT_RED);
  display.drawCircle(w / 2, h / 4, 15, TFT_GREEN);

  int x = 44;
  for (uint8_t scale = 1; scale <= 4; scale++) {
    display.drawBitmapScaled(x, h / 4 - 16, kDemoBitmap, 8, 8, TFT_YELLOW, scale);
    x += 8 * scale + 6;
  }

  display.setTextColor(TFT_WHITE);
  display.setTextSize(1);
  int y = h / 2;
  for (uint8_t font = 1; font <= 4; font++) {
    display.setTextFont(font);
    display.setCursor(10, y);
    display.printf("Font %u: 0123 ABC", (unsigned)font);
    y += (int)display.fontHeight() + 4;
    if (y >= h - 10) break;
  }
}

// Порядок демонстрації в авто-циклі ("test-gfx on" без імені патерну) -
// той самий порядок, у якому оголошено enum. Єдиний список: testGfxPatternFromName()
// і testGfxNextPattern() використовують саме його, а не власні копії.
constexpr TestGfxPattern kAllPatterns[] = {
    TestGfxPattern::Bars,  TestGfxPattern::Gray,    TestGfxPattern::Gradient,
    TestGfxPattern::Frame, TestGfxPattern::Checker, TestGfxPattern::Primitives,
};
constexpr int kPatternCount = sizeof(kAllPatterns) / sizeof(kAllPatterns[0]);

}  // namespace

const char* testGfxPatternName(TestGfxPattern p) {
  switch (p) {
    case TestGfxPattern::Bars: return "bars";
    case TestGfxPattern::Gray: return "gray";
    case TestGfxPattern::Gradient: return "gradient";
    case TestGfxPattern::Frame: return "frame";
    case TestGfxPattern::Checker: return "checker";
    case TestGfxPattern::Primitives: return "primitives";
  }
  return "?";
}

bool testGfxPatternFromName(const char* name, TestGfxPattern* out) {
  for (TestGfxPattern p : kAllPatterns) {
    if (strcasecmp(name, testGfxPatternName(p)) == 0) {
      *out = p;
      return true;
    }
  }
  return false;
}

TestGfxPattern testGfxNextPattern(TestGfxPattern p) {
  for (int i = 0; i < kPatternCount; i++) {
    if (kAllPatterns[i] == p) return kAllPatterns[(i + 1) % kPatternCount];
  }
  return kAllPatterns[0];
}

void drawTestGfx(TestGfxPattern pattern) {
  const int w = display.width();
  const int h = display.height();
  if (w <= 0 || h <= 0) return;

  switch (pattern) {
    case TestGfxPattern::Bars:
      fillGrid(0, h, 6, 1, barColorAt);
      return;
    case TestGfxPattern::Gray: {
      const int rampH = h * 7 / 10;
      fillGrid(0, rampH, 16, 1, grayRampAt);
      fillGrid(rampH, h - rampH, 5, 1, grayPatchAt);
      return;
    }
    case TestGfxPattern::Gradient: {
      const int halfH = h / 2;
      fillGrid(0, halfH, 32, 3, gradientRampAt);
      fillGrid(halfH, h - halfH, 16, 16, paletteAt);
      return;
    }
    case TestGfxPattern::Frame:
      drawFrame(w, h);
      return;
    case TestGfxPattern::Checker:
      fillGrid(0, h, 16, 16, checkerAt);
      return;
    case TestGfxPattern::Primitives:
      drawPrimitives(w, h);
      return;
  }
}
