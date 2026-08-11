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

bool ImageEffects::applyDithering(JpegImage &image) {
  if (image.colorDepth() != JpegColorDepth::RGB332) {
    _logger.warn("dithering: підтримується лише RGB332");
    return false;
  }
  if (!image.isLoaded()) {
    return false;
  }

  const float spread = 0.14f;  // інтенсивність шуму під 3-бітну сітку каналів
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
  return true;
}

// Box blur у 2 проходи, тимчасовий буфер лише на один рядок/стовпець (не на весь кадр) -
// напр. для 480 px рядка це 480 * sizeof(Pixel) = 5.7 КБ проти ~2.7 МБ на весь кадр.
bool ImageEffects::applyBoxBlur(JpegImage &image, uint8_t radius, uint8_t passes) {
  if (radius < 1) {
    return false;
  }
  if (image.colorDepth() == JpegColorDepth::MONO1) {
    _logger.warn("blur: MONO1 не підтримується");
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
    _logger.error("blur: не вистачило пам'яті на line buffer (%u px)", maxDim);
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
