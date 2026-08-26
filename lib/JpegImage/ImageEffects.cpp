#include "ImageEffects.hpp"

#include <TLogger.hpp>
#include <cstdlib>

namespace {
const TLogger _logger{"imgfx"};

// Матриця Байєра 8x8, масштабована в діапазон [-0.5 .. 0.5]
const float kBayer8x8[8][8] = {
    {-0.5000f,  0.2500f, -0.3125f,  0.4375f, -0.4531f,  0.2969f, -0.2656f,  0.4844f},
    { 0.1250f, -0.1250f,  0.3125f,  0.0625f,  0.1719f, -0.0781f,  0.3594f,  0.2188f},
    {-0.3750f,  0.3750f, -0.4375f,  0.1875f, -0.3281f,  0.4219f, -0.3906f,  0.3281f},
    { 0.2500f,  0.0000f,  0.1250f, -0.2500f,  0.2344f, -0.0156f,  0.1094f, -0.1406f},
    {-0.4688f,  0.2813f, -0.2813f,  0.4688f, -0.4844f,  0.2656f, -0.3438f,  0.4531f},
    { 0.1563f, -0.0938f,  0.3438f,  0.2031f,  0.0938f, -0.1563f,  0.2813f,  0.0313f},
    {-0.3438f,  0.4063f, -0.4063f,  0.3125f, -0.3594f,  0.3906f, -0.4219f,  0.1563f},
    { 0.2188f, -0.0313f,  0.0938f, -0.1563f,  0.2031f, -0.0469f,  0.0625f, -0.2188f},
};
}  // namespace

Pixel ImageEffects::getPixel(const JpegImage &image, size_t index) {
  switch (image.colorDepth()) {
    case JpegColorDepth::RGB332:
      return Pixel::unpack(((const uint8_t *)image.buffer())[index]);
    case JpegColorDepth::RGB565:
      return Pixel::unpack(((const uint16_t *)image.buffer())[index]);
    case JpegColorDepth::RGB888: {
      const uint8_t *px = ((const uint8_t *)image.buffer()) + index * 3;
      return Pixel::fromRGB888(px[0], px[1], px[2]);
    }
    default:
      return Pixel{};
  }
}

void ImageEffects::setPixel(JpegImage &image, size_t index, const Pixel &p) {
  switch (image.colorDepth()) {
    case JpegColorDepth::RGB332:
      ((uint8_t *)image.buffer())[index] = p.pack<uint8_t>();
      return;
    case JpegColorDepth::RGB565:
      ((uint16_t *)image.buffer())[index] = p.pack<uint16_t>();
      return;
    case JpegColorDepth::RGB888:
      p.packRGB888(((uint8_t *)image.buffer()) + index * 3);
      return;
    default:
      return;
  }
}

bool ImageEffects::applyDesaturate(JpegImage &image, float factor) {
  return applyPerPixel(image, [&](Pixel &p) { p.fxDesaturate(factor); });
}

bool ImageEffects::applyLighten(JpegImage &image, float factor) {
  return applyPerPixel(image, [&](Pixel &p) { p.fxLighten(factor); });
}

bool ImageEffects::applyDarken(JpegImage &image, float factor) {
  return applyPerPixel(image, [&](Pixel &p) { p.fxDarken(factor); });
}

bool ImageEffects::applyTint(JpegImage &image, const Pixel &tint, float alpha) {
  return applyPerPixel(image, [&](Pixel &p) { p.fxTint(tint, alpha); });
}

bool ImageEffects::applyContrast(JpegImage &image, float contrast) {
  return applyPerPixel(image, [&](Pixel &p) { p.fxContrast(contrast); });
}

bool ImageEffects::applySepia(JpegImage &image, float amount) {
  return applyPerPixel(image, [&](Pixel &p) { p.fxSepia(amount); });
}

bool ImageEffects::applyHueRotate(JpegImage &image, float angleRad) {
  return applyPerPixel(image, [&](Pixel &p) { p.fxHueRotate(angleRad); });
}

bool ImageEffects::applyThermal(JpegImage &image) {
  return applyPerPixel(image, [](Pixel &p) { p.fxThermal(); });
}

bool ImageEffects::applyInvert(JpegImage &image) {
  return applyPerPixel(image, [](Pixel &p) { p.fxInvert(); });
}

bool ImageEffects::applyThreshold(JpegImage &image, float threshold) {
  return applyPerPixel(image, [&](Pixel &p) { p.fxThreshold(threshold); });
}

bool ImageEffects::applyGamma(JpegImage &image, float gamma) {
  return applyPerPixel(image, [&](Pixel &p) { p.fxGamma(gamma); });
}

bool ImageEffects::applyPosterize(JpegImage &image, int levels) {
  return applyPerPixel(image, [&](Pixel &p) { p.fxPosterize(levels); });
}

bool ImageEffects::applySolarize(JpegImage &image, float threshold) {
  return applyPerPixel(image, [&](Pixel &p) { p.fxSolarize(threshold); });
}

bool ImageEffects::applyDuotone(JpegImage &image, const Pixel &dark, const Pixel &light) {
  return applyPerPixel(image, [&](Pixel &p) { p.fxDuotone(dark, light); });
}

bool ImageEffects::applyColorBalance(JpegImage &image, float rMul, float gMul, float bMul) {
  return applyPerPixel(image, [&](Pixel &p) { p.fxColorBalance(rMul, gMul, bMul); });
}

bool ImageEffects::applyNoise(JpegImage &image, float amount) {
  return applyPerPixel(image, [&](Pixel &p) {
    p.r += (random(-1000, 1001) / 1000.0f) * amount;
    p.g += (random(-1000, 1001) / 1000.0f) * amount;
    p.b += (random(-1000, 1001) / 1000.0f) * amount;
  });
}

void ImageEffects::applyOrderedDither(JpegImage &image, float spread) {
  uint16_t w = image.width();
  uint16_t h = image.height();

  for (uint16_t y = 0; y < h; y++) {
    for (uint16_t x = 0; x < w; x++) {
      size_t idx = (size_t)y * w + x;
      Pixel p = getPixel(image, idx);
      float noise = kBayer8x8[y & 7][x & 7] * spread;
      p.r += noise;
      p.g += noise;
      p.b += noise;
      setPixel(image, idx, p);
    }
  }
}

bool ImageEffects::applyDitheringRGB332(JpegImage &image) {
  if (image.colorDepth() != JpegColorDepth::RGB332) {
    _logger.warn("ditheringRGB332: image is not RGB332");
    return false;
  }
  if (!image.isLoaded()) {
    return false;
  }
  // 3-бітні R/G (крок 1/7) і 2-бітний B (крок 1/3) - беремо спред під R/G,
  // саме вони найпомітніші для ока, B все одно "грубий" незалежно від дизерингу.
  applyOrderedDither(image, 0.14f);
  return true;
}

bool ImageEffects::applyDitheringRGB565(JpegImage &image) {
  if (image.colorDepth() != JpegColorDepth::RGB565) {
    _logger.warn("ditheringRGB565: image is not RGB565");
    return false;
  }
  if (!image.isLoaded()) {
    return false;
  }
  // 5-бітні R/B (крок 1/31 = 0.032) і 6-бітний G (крок 1/63 = 0.016) - спред
  // між ними, ближче до R/B (вони "грубіші" й банди на них помітніші).
  applyOrderedDither(image, 0.03f);
  return true;
}

bool ImageEffects::applyDitheringRGB888(JpegImage &image) {
  if (image.colorDepth() != JpegColorDepth::RGB888) {
    _logger.warn("ditheringRGB888: image is not RGB888");
    return false;
  }
  if (!image.isLoaded()) {
    return false;
  }
  // 8-бітний крок (1/255 = 0.0039) - оку майже непомітний. Метод існує заради
  // єдиного інтерфейсу команд для всіх плат; візуального ефекту на RGB888
  // практично не буде (тут немає подальшого квантування, яке дизеринг мав би
  // "розмити").
  applyOrderedDither(image, 0.004f);
  return true;
}

// Box blur у 2 проходи, тимчасовий буфер лише на один рядок/стовпець (не на весь кадр) -
// напр. для 480 px рядка це 480 * sizeof(Pixel) = 5.7 КБ проти ~2.7 МБ на весь кадр.
bool ImageEffects::applyBoxBlur(JpegImage &image, uint8_t radius, uint8_t passes) {
  if (radius < 1) {
    return false;
  }
  if (image.colorDepth() == JpegColorDepth::MONO1) {
    _logger.warn("blur: MONO1 not supported");
    return false;
  }
  if (!image.isLoaded()) {
    return false;
  }

  uint16_t w = image.width();
  uint16_t h = image.height();
  uint16_t maxDim = (w > h) ? w : h;

  Pixel *lineBuffer = (Pixel *)malloc(maxDim * sizeof(Pixel));
  if (lineBuffer == nullptr) {
    _logger.error("blur: out of memory for line buffer (%u px)", maxDim);
    return false;
  }

  for (uint8_t pass = 0; pass < passes; pass++) {
    // Горизонтальний прохід
    for (uint16_t y = 0; y < h; y++) {
      for (uint16_t x = 0; x < w; x++) {
        lineBuffer[x] = getPixel(image, (size_t)y * w + x);
      }
      for (uint16_t x = 0; x < w; x++) {
        float rSum = 0, gSum = 0, bSum = 0;
        int count = 0;
        for (int k = -radius; k <= radius; k++) {
          int nx = x + k;
          if (nx >= 0 && nx < w) {
            rSum += lineBuffer[nx].r;
            gSum += lineBuffer[nx].g;
            bSum += lineBuffer[nx].b;
            count++;
          }
        }
        setPixel(image, (size_t)y * w + x, Pixel{rSum / count, gSum / count, bSum / count});
      }
    }

    // Вертикальний прохід
    for (uint16_t x = 0; x < w; x++) {
      for (uint16_t y = 0; y < h; y++) {
        lineBuffer[y] = getPixel(image, (size_t)y * w + x);
      }
      for (uint16_t y = 0; y < h; y++) {
        float rSum = 0, gSum = 0, bSum = 0;
        int count = 0;
        for (int k = -radius; k <= radius; k++) {
          int ny = y + k;
          if (ny >= 0 && ny < h) {
            rSum += lineBuffer[ny].r;
            gSum += lineBuffer[ny].g;
            bSum += lineBuffer[ny].b;
            count++;
          }
        }
        setPixel(image, (size_t)y * w + x, Pixel{rSum / count, gSum / count, bSum / count});
      }
    }
  }

  free(lineBuffer);
  return true;
}

bool ImageEffects::applyVignette(JpegImage &image, float strength) {
  if (image.colorDepth() == JpegColorDepth::MONO1) {
    _logger.warn("vignette: MONO1 not supported");
    return false;
  }
  if (!image.isLoaded()) {
    return false;
  }
  if (strength < 0.0f) strength = 0.0f;
  if (strength > 1.0f) strength = 1.0f;

  uint16_t w = image.width();
  uint16_t h = image.height();
  float cx = w / 2.0f;
  float cy = h / 2.0f;
  float maxDist = sqrtf(cx * cx + cy * cy);
  if (maxDist < 1.0f) maxDist = 1.0f;

  for (uint16_t y = 0; y < h; y++) {
    for (uint16_t x = 0; x < w; x++) {
      float dx = x - cx;
      float dy = y - cy;
      float dist = sqrtf(dx * dx + dy * dy) / maxDist;  // 0.0 в центрі .. ~1.0 у кутах
      float darken = 1.0f - strength * dist * dist;
      if (darken < 0.0f) darken = 0.0f;

      size_t idx = (size_t)y * w + x;
      Pixel p = getPixel(image, idx);
      p.fxDarken(darken);
      setPixel(image, idx, p);
    }
  }
  return true;
}

bool ImageEffects::applyPixelate(JpegImage &image, uint8_t blockSize) {
  if (blockSize < 2) {
    return false;
  }
  if (image.colorDepth() == JpegColorDepth::MONO1) {
    _logger.warn("pixelate: MONO1 not supported");
    return false;
  }
  if (!image.isLoaded()) {
    return false;
  }

  uint16_t w = image.width();
  uint16_t h = image.height();

  for (uint16_t by = 0; by < h; by += blockSize) {
    uint16_t bh = ((uint16_t)(by + blockSize) <= h) ? blockSize : (h - by);
    for (uint16_t bx = 0; bx < w; bx += blockSize) {
      uint16_t bw = ((uint16_t)(bx + blockSize) <= w) ? blockSize : (w - bx);

      float rSum = 0, gSum = 0, bSum = 0;
      uint32_t count = 0;
      for (uint16_t y = by; y < by + bh; y++) {
        for (uint16_t x = bx; x < bx + bw; x++) {
          Pixel p = getPixel(image, (size_t)y * w + x);
          rSum += p.r;
          gSum += p.g;
          bSum += p.b;
          count++;
        }
      }
      Pixel avg{rSum / count, gSum / count, bSum / count};

      for (uint16_t y = by; y < by + bh; y++) {
        for (uint16_t x = bx; x < bx + bw; x++) {
          setPixel(image, (size_t)y * w + x, avg);
        }
      }
    }
  }
  return true;
}

bool ImageEffects::applyScanlines(JpegImage &image, float darkenFactor) {
  if (image.colorDepth() == JpegColorDepth::MONO1) {
    _logger.warn("scanlines: MONO1 not supported");
    return false;
  }
  if (!image.isLoaded()) {
    return false;
  }

  uint16_t w = image.width();
  uint16_t h = image.height();

  for (uint16_t y = 1; y < h; y += 2) {
    for (uint16_t x = 0; x < w; x++) {
      size_t idx = (size_t)y * w + x;
      Pixel p = getPixel(image, idx);
      p.fxDarken(darkenFactor);
      setPixel(image, idx, p);
    }
  }
  return true;
}

bool ImageEffects::applyChromaticAberration(JpegImage &image, uint8_t offsetPx) {
  if (offsetPx < 1) {
    return false;
  }
  if (image.colorDepth() == JpegColorDepth::MONO1) {
    _logger.warn("chromaticAberration: MONO1 not supported");
    return false;
  }
  if (!image.isLoaded()) {
    return false;
  }

  uint16_t w = image.width();
  uint16_t h = image.height();

  Pixel *lineBuffer = (Pixel *)malloc(w * sizeof(Pixel));
  if (lineBuffer == nullptr) {
    _logger.error("chromaticAberration: out of memory for line buffer (%u px)", w);
    return false;
  }

  for (uint16_t y = 0; y < h; y++) {
    for (uint16_t x = 0; x < w; x++) {
      lineBuffer[x] = getPixel(image, (size_t)y * w + x);
    }
    for (uint16_t x = 0; x < w; x++) {
      int rx = (int)x - offsetPx;
      if (rx < 0) rx = 0;
      int bx = (int)x + offsetPx;
      if (bx >= w) bx = w - 1;

      Pixel out{lineBuffer[rx].r, lineBuffer[x].g, lineBuffer[bx].b};
      setPixel(image, (size_t)y * w + x, out);
    }
  }

  free(lineBuffer);
  return true;
}

// Спільна техніка для 3x3-згорток (Sobel/emboss): 3 рядкові буфери (prev/cur/next),
// що котяться вниз по кадру - не потрібен буфер на весь кадр. Крайні пікселі рядка/
// кадру - clamp-to-edge (дублювання межового пікселя/рядка).
namespace {
float lumaAtClamped(const Pixel *row, int x, uint16_t w) {
  if (x < 0) x = 0;
  if (x >= w) x = w - 1;
  return row[x].lumaRec709();
}
}  // namespace

bool ImageEffects::applySobelEdges(JpegImage &image) {
  if (image.colorDepth() == JpegColorDepth::MONO1) {
    _logger.warn("sobelEdges: MONO1 not supported");
    return false;
  }
  if (!image.isLoaded()) {
    return false;
  }

  uint16_t w = image.width();
  uint16_t h = image.height();
  if (w < 3 || h < 3) {
    _logger.warn("sobelEdges: at least 3x3 pixels required");
    return false;
  }

  Pixel *prevRow = (Pixel *)malloc(w * sizeof(Pixel));
  Pixel *curRow = (Pixel *)malloc(w * sizeof(Pixel));
  Pixel *nextRow = (Pixel *)malloc(w * sizeof(Pixel));
  if (prevRow == nullptr || curRow == nullptr || nextRow == nullptr) {
    _logger.error("sobelEdges: out of memory for 3 line buffers (%u px each)", w);
    free(prevRow);
    free(curRow);
    free(nextRow);
    return false;
  }

  for (uint16_t x = 0; x < w; x++) {
    curRow[x] = getPixel(image, x);            // рядок y=0
    nextRow[x] = getPixel(image, (size_t)w + x);  // рядок y=1
    prevRow[x] = curRow[x];                     // clamp-to-edge для верхньої межі
  }

  for (uint16_t y = 0; y < h; y++) {
    for (uint16_t x = 0; x < w; x++) {
      int xi = x;
      float gx = -lumaAtClamped(prevRow, xi - 1, w) + lumaAtClamped(prevRow, xi + 1, w) -
                 2.0f * lumaAtClamped(curRow, xi - 1, w) + 2.0f * lumaAtClamped(curRow, xi + 1, w) -
                 lumaAtClamped(nextRow, xi - 1, w) + lumaAtClamped(nextRow, xi + 1, w);
      float gy = -lumaAtClamped(prevRow, xi - 1, w) - 2.0f * lumaAtClamped(prevRow, xi, w) -
                 lumaAtClamped(prevRow, xi + 1, w) + lumaAtClamped(nextRow, xi - 1, w) +
                 2.0f * lumaAtClamped(nextRow, xi, w) + lumaAtClamped(nextRow, xi + 1, w);

      float mag = sqrtf(gx * gx + gy * gy);
      if (mag > 1.0f) mag = 1.0f;
      setPixel(image, (size_t)y * w + x, Pixel{mag, mag, mag});
    }

    // Зсув вікна на рядок вниз: те, що щойно записали в image (рядок y), більше не
    // читаємо - наступному кроку потрібні лише вже кешовані prev/cur/next.
    Pixel *tmp = prevRow;
    prevRow = curRow;
    curRow = nextRow;
    nextRow = tmp;

    uint16_t nextY = ((uint16_t)(y + 2) < h) ? (uint16_t)(y + 2) : (h - 1);
    for (uint16_t x = 0; x < w; x++) {
      nextRow[x] = getPixel(image, (size_t)nextY * w + x);
    }
  }

  free(prevRow);
  free(curRow);
  free(nextRow);
  return true;
}

bool ImageEffects::applyEmboss(JpegImage &image, float strength) {
  if (image.colorDepth() == JpegColorDepth::MONO1) {
    _logger.warn("emboss: MONO1 not supported");
    return false;
  }
  if (!image.isLoaded()) {
    return false;
  }

  uint16_t w = image.width();
  uint16_t h = image.height();
  if (w < 3 || h < 3) {
    _logger.warn("emboss: at least 3x3 pixels required");
    return false;
  }

  Pixel *prevRow = (Pixel *)malloc(w * sizeof(Pixel));
  Pixel *curRow = (Pixel *)malloc(w * sizeof(Pixel));
  Pixel *nextRow = (Pixel *)malloc(w * sizeof(Pixel));
  if (prevRow == nullptr || curRow == nullptr || nextRow == nullptr) {
    _logger.error("emboss: out of memory for 3 line buffers (%u px each)", w);
    free(prevRow);
    free(curRow);
    free(nextRow);
    return false;
  }

  for (uint16_t x = 0; x < w; x++) {
    curRow[x] = getPixel(image, x);
    nextRow[x] = getPixel(image, (size_t)w + x);
    prevRow[x] = curRow[x];
  }

  // Emboss-матриця з НУЛЬОВОЮ сумою, масштабована на strength:
  //   [-2 -1  0]
  //   [-1  0  1]
  //   [ 0  1  2]
  // Саме нульова сума + bias +0.5f нижче дають класичний вигляд emboss:
  // рівні ділянки стають нейтрально-сірими, видно лише рельєф на перепадах.
  //
  // Тут НЕМАЄ центрального члена (+1 * curRow[x]) - раніше коментар описував
  // варіант матриці з одиницею в центрі (сума = 1), який зберігає оригінальну
  // яскравість і bias-у не потребує. Реалізовано інший, нульовий варіант;
  // коментар приведено у відповідність до коду, а не навпаки, щоб не змінювати
  // вже налаштований візуальний результат.
  for (uint16_t y = 0; y < h; y++) {
    for (uint16_t x = 0; x < w; x++) {
      int xi = x;
      float conv = strength * (-2.0f * lumaAtClamped(prevRow, xi - 1, w) -
                                1.0f * lumaAtClamped(prevRow, xi, w) +
                                0.0f * lumaAtClamped(prevRow, xi + 1, w) -
                                1.0f * lumaAtClamped(curRow, xi - 1, w) +
                                1.0f * lumaAtClamped(curRow, xi + 1, w) +
                                0.0f * lumaAtClamped(nextRow, xi - 1, w) +
                                1.0f * lumaAtClamped(nextRow, xi, w) +
                                2.0f * lumaAtClamped(nextRow, xi + 1, w));
      float val = conv + 0.5f;
      if (val < 0.0f) val = 0.0f;
      if (val > 1.0f) val = 1.0f;
      setPixel(image, (size_t)y * w + x, Pixel{val, val, val});
    }

    Pixel *tmp = prevRow;
    prevRow = curRow;
    curRow = nextRow;
    nextRow = tmp;

    uint16_t nextY = ((uint16_t)(y + 2) < h) ? (uint16_t)(y + 2) : (h - 1);
    for (uint16_t x = 0; x < w; x++) {
      nextRow[x] = getPixel(image, (size_t)nextY * w + x);
    }
  }

  free(prevRow);
  free(curRow);
  free(nextRow);
  return true;
}
