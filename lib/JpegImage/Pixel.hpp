#pragma once

#include <Arduino.h>

#include <cmath>

// Використання:
//   Pixel p = Pixel::unpack(rawColor);       // rawColor: uint8_t|uint16_t|uint32_t -
//                                             // компілятор сам обирає RGB332/RGB565/RGB888
//   p.fxDesaturate(0.2f).fxDarken(0.6f);      // ланцюжок поодиноких пер-піксельних ефектів
//   rawColor = p.pack<uint16_t>();            // назад у той самий формат, з якого розпакували
//
// Pixel - легкий value-type (3 float, POD, без vtable), призначений для стекового
// використання у гарячому циклі "unpack -> fx* -> pack" по одному пікселю за раз.
// НЕ тримати масив Pixel[] на весь кадр - на платах без PSRAM (esp32-st7789, ttgo-t1,
// esp8266) 480x480 чи навіть 240x320 float-буфер просто не влізе в пам'ять.
struct Pixel {
  float r = 0.0f;
  float g = 0.0f;
  float b = 0.0f;

  // --- Розпаковка: формат визначається типом аргументу (RGB332/RGB565/RGB888) ---

  static Pixel unpack(uint8_t color) {
    return Pixel{
        ((color >> 5) & 0x07) / 7.0f,
        ((color >> 2) & 0x07) / 7.0f,
        (color & 0x03) / 3.0f,
    };
  }

  static Pixel unpack(uint16_t color) {
    return Pixel{
        ((color >> 11) & 0x1F) / 31.0f,
        ((color >> 5) & 0x3F) / 63.0f,
        (color & 0x1F) / 31.0f,
    };
  }

  // RGB888, запаковане як 0x00RRGGBB
  static Pixel unpack(uint32_t color) {
    return Pixel{
        ((color >> 16) & 0xFF) / 255.0f,
        ((color >> 8) & 0xFF) / 255.0f,
        (color & 0xFF) / 255.0f,
    };
  }

  static Pixel fromRGB888(uint8_t r8, uint8_t g8, uint8_t b8) {
    return Pixel{r8 / 255.0f, g8 / 255.0f, b8 / 255.0f};
  }

  // --- Запаковка: формат визначається типом шаблонного параметра ---
  // Приклад: uint16_t raw = p.pack<uint16_t>();

  template <typename T>
  T pack() const;

  // Запис RGB888 у три окремі байти (порядок R,G,B) - для JpegImage::bufferRGB888()
  void packRGB888(uint8_t *destPixel) const {
    Pixel c = clamped();
    destPixel[0] = (uint8_t)(c.r * 255.0f + 0.5f);
    destPixel[1] = (uint8_t)(c.g * 255.0f + 0.5f);
    destPixel[2] = (uint8_t)(c.b * 255.0f + 0.5f);
  }

  // --- Пер-піксельні ефекти (ланцюжок, кожен повертає *this) ---

  // factor: 0.0f = повна сірість (Ч/Б), 1.0f = оригінальні кольори
  Pixel &fxDesaturate(float factor) {
    float luma = lumaRec709();
    r = luma + (r - luma) * factor;
    g = luma + (g - luma) * factor;
    b = luma + (b - luma) * factor;
    return *this;
  }

  // factor: 0.0f = без змін, 1.0f = у чисто білий
  Pixel &fxLighten(float factor) {
    r += (1.0f - r) * factor;
    g += (1.0f - g) * factor;
    b += (1.0f - b) * factor;
    return *this;
  }

  // factor: 1.0f = без змін, 0.0f = у чорний
  Pixel &fxDarken(float factor) {
    r *= factor;
    g *= factor;
    b *= factor;
    return *this;
  }

  // alpha: 0.0f = лише оригінал, 1.0f = повністю колір tint
  Pixel &fxTint(const Pixel &tint, float alpha) {
    r = r * (1.0f - alpha) + tint.r * alpha;
    g = g * (1.0f - alpha) + tint.g * alpha;
    b = b * (1.0f - alpha) + tint.b * alpha;
    return *this;
  }

  // contrast: 1.0f = оригінал, >1.0f - вище (наприклад 1.5f)
  Pixel &fxContrast(float contrast) {
    r = (r - 0.5f) * contrast + 0.5f;
    g = (g - 0.5f) * contrast + 0.5f;
    b = (b - 0.5f) * contrast + 0.5f;
    return *this;
  }

  // Сепія за класичною фотографічною матрицею, amount - вага відносно оригіналу
  Pixel &fxSepia(float amount = 1.0f) {
    Pixel sepia{
        r * 0.393f + g * 0.769f + b * 0.189f,
        r * 0.349f + g * 0.686f + b * 0.168f,
        r * 0.272f + g * 0.534f + b * 0.131f,
    };
    return fxTint(sepia.clamped(), amount);
  }

  // Інверсія кольору (негатив)
  Pixel &fxInvert() {
    r = 1.0f - r;
    g = 1.0f - g;
    b = 1.0f - b;
    return *this;
  }

  // Порогова бінаризація (Ч/Б без відтінків)
  Pixel &fxThreshold(float threshold) {
    float val = (lumaRec709() >= threshold) ? 1.0f : 0.0f;
    r = g = b = val;
    return *this;
  }

  // gamma: 1.0f = без змін, >1.0f - світліше в напівтонах, <1.0f - темніше
  Pixel &fxGamma(float gamma) {
    float invGamma = 1.0f / gamma;
    r = powf(clampf(r), invGamma);
    g = powf(clampf(g), invGamma);
    b = powf(clampf(b), invGamma);
    return *this;
  }

  // levels: кількість рівнів на канал (>=2). 2 = чистий Ч/Б по кожному каналу,
  // 4-8 - плаский "постер"-вигляд. Банди, які лишає posterize, добре маскуються
  // подальшим applyDitheringRGB* (ImageEffects)
  Pixel &fxPosterize(int levels) {
    if (levels < 2) {
      return *this;
    }
    float steps = (float)(levels - 1);
    r = roundf(clampf(r) * steps) / steps;
    g = roundf(clampf(g) * steps) / steps;
    b = roundf(clampf(b) * steps) / steps;
    return *this;
  }

  // threshold: 0.0-1.0. Кожен канал вище порогу інвертується окремо (класичний
  // фотографічний solarize - на відміну від fxThreshold, лишає кольори, не Ч/Б)
  Pixel &fxSolarize(float threshold) {
    if (r > threshold) r = 1.0f - r;
    if (g > threshold) g = 1.0f - g;
    if (b > threshold) b = 1.0f - b;
    return *this;
  }

  // Дуотон: інтерполяція між dark і light за яскравістю пікселя.
  // fxSepia - окремий випадок duotone з фіксованою теплою парою кольорів.
  Pixel &fxDuotone(const Pixel &dark, const Pixel &light) {
    float luma = lumaRec709();
    r = dark.r + (light.r - dark.r) * luma;
    g = dark.g + (light.g - dark.g) * luma;
    b = dark.b + (light.b - dark.b) * luma;
    return *this;
  }

  // Незалежне множення каналів - теплий/холодний баланс без зміни яскравості/контрасту.
  // rMul/gMul/bMul: 1.0f = без змін, приклад для теплішого відтінку: (1.1f, 1.0f, 0.9f)
  Pixel &fxColorBalance(float rMul, float gMul, float bMul) {
    r *= rMul;
    g *= gMul;
    b *= bMul;
    return *this;
  }

  // angle: радіани (0 .. 2*PI) - обертання Hue навколо осі яскравості
  Pixel &fxHueRotate(float angle) {
    float cosA = cosf(angle);
    float sinA = sinf(angle);
    const float third = 1.0f / 3.0f;
    const float root = sqrtf(third);

    float nr = (cosA + (1.0f - cosA) * third) * r +
               ((1.0f - cosA) * third - root * sinA) * g +
               ((1.0f - cosA) * third + root * sinA) * b;
    float ng = ((1.0f - cosA) * third + root * sinA) * r +
               (cosA + (1.0f - cosA) * third) * g +
               ((1.0f - cosA) * third - root * sinA) * b;
    float nb = ((1.0f - cosA) * third - root * sinA) * r +
               ((1.0f - cosA) * third + root * sinA) * g +
               (cosA + (1.0f - cosA) * third) * b;

    r = nr;
    g = ng;
    b = nb;
    return *this;
  }

  // Тепловізор: відкидає оригінальний колір, фарбує за яскравістю
  // (темне -> синє, середнє -> зелено-жовте, яскраве -> червоно-біле)
  Pixel &fxThermal() {
    float luma = lumaRec709();
    r = clampf((luma - 0.4f) * 3.0f);
    g = clampf(1.0f - fabsf(luma - 0.5f) * 3.0f);
    b = clampf((0.6f - luma) * 3.0f);
    return *this;
  }

  float lumaRec709() const { return 0.2126f * r + 0.7152f * g + 0.0722f * b; }

  Pixel clamped() const { return Pixel{clampf(r), clampf(g), clampf(b)}; }

 private:
  static float clampf(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
};

template <>
inline uint8_t Pixel::pack<uint8_t>() const {
  Pixel c = clamped();
  return ((uint8_t)(c.r * 7.0f + 0.5f) << 5) | ((uint8_t)(c.g * 7.0f + 0.5f) << 2) |
         (uint8_t)(c.b * 3.0f + 0.5f);
}

template <>
inline uint16_t Pixel::pack<uint16_t>() const {
  Pixel c = clamped();
  return ((uint16_t)(c.r * 31.0f + 0.5f) << 11) | ((uint16_t)(c.g * 63.0f + 0.5f) << 5) |
         (uint16_t)(c.b * 31.0f + 0.5f);
}

template <>
inline uint32_t Pixel::pack<uint32_t>() const {
  Pixel c = clamped();
  return ((uint32_t)(c.r * 255.0f + 0.5f) << 16) | ((uint32_t)(c.g * 255.0f + 0.5f) << 8) |
         (uint32_t)(c.b * 255.0f + 0.5f);
}
