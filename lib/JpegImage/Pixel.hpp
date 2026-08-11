#pragma once

#include <stdio.h>

/// Структура для промежуточных вычислений с плавающей точкой (диапазон 0.0f ... 1.0f)
class Pixel {
public:
  float r;
  float g;
  float b;

  void unpack(uint8_t color)  { 
    r = ((color >> 5) & 0x07) /    7.0f;
    g = ((color >> 2) & 0x07) /    7.0f;
    b = ((color >> 0) & 0x03) /    3.0f;
  }

  void unpack(uint16_t color) { 
    r = ((color >> 11) & 0x1F) /  31.0f;
    g = ((color >>  5) & 0x3F) /  63.0f;
    b = ((color >>  0) & 0x1F) /  31.0f;
  }

  void unpack(uint32_t color) { 
    r = ((color >> 16) & 0xFF) / 255.0f;
    g = ((color >>  8) & 0xFF) / 255.0f;
    b = ((color >>  0) & 0xFF) / 255.0f;
  }

  void pack_pixel(uint8_t& out) {
    if (r < 0.0f) r = 0.0f; else if (r > 1.0f) r = 1.0f;
    if (g < 0.0f) g = 0.0f; else if (g > 1.0f) g = 1.0f;
    if (b < 0.0f) b = 0.0f; else if (b > 1.0f) b = 1.0f;

    out = ((uint8_t)(r * 7.0f + 0.5f) << 5) | 
          ((uint8_t)(g * 7.0f + 0.5f) << 2) | 
          ((uint8_t)(b * 3.0f + 0.5f) << 0);
  }

  void pack_pixel(uint16_t& out) {
      if (r < 0.0f) r = 0.0f; else if (r > 1.0f) r = 1.0f;
      if (g < 0.0f) g = 0.0f; else if (g > 1.0f) g = 1.0f;
      if (b < 0.0f) b = 0.0f; else if (b > 1.0f) b = 1.0f;

      out = ((uint16_t)(r * 31.0f + 0.5f) << 11) |
            ((uint16_t)(g * 63.0f + 0.5f) <<  5) |
            ((uint16_t)(b * 31.0f + 0.5f) <<  0);
  }

  void pack_pixel(uint32_t& out) {
      if (r < 0.0f) r = 0.0f; else if (r > 1.0f) r = 1.0f;
      if (g < 0.0f) g = 0.0f; else if (g > 1.0f) g = 1.0f;
      if (b < 0.0f) b = 0.0f; else if (b > 1.0f) b = 1.0f;

      out = ((uint32_t)(r * 255.0f + 0.5f) << 16) |
            ((uint32_t)(g * 255.0f + 0.5f) <<  8) |
            ((uint32_t)(b * 255.0f + 0.5f) <<  0);
  }

  uint8_t  asRGB332() { uint8_t  out; pack_pixel(out); return out; }
  uint16_t asRGB565() { uint16_t out; pack_pixel(out); return out; }
  uint32_t asRGB888() { uint32_t out; pack_pixel(out); return out; }

  // 1. ДЕСАТУРАЦИЯ И ГАММА-КОРРЕКЦИЯ (Luma Rec. 709)
  // factor: 0.0f = полная серость (ЧБ), 1.0f = оригинальные цвета.
  Pixel& fx_desaturate(float factor) {
    // Рассчитываем светимость с учетом физиологии человеческого глаза (зеленый ярче всего)
    float luma = 0.2126f * r + 0.7152f * g + 0.0722f * b;
    
    // Линейная интерполяция между ЧБ спектром и цветом
    r = luma + (r - luma) * factor;
    g = luma + (g - luma) * factor;
    b = luma + (b - luma) * factor;

    return *this;
  }

  // 2. ОСВЕТЛЕНИЕ (Light Watermark эффекты)
  // factor: 0.0f = без изменений, 1.0f = превратить в чисто белый фон
  Pixel& fx_lighten(float factor) {
    r = r + (1.0f - r) * factor;
    g = g + (1.0f - g) * factor;
    b = b + (1.0f - b) * factor;

    return *this;
  }

  // 3. ЗАТЕМНЕНИЕ (Dark UI / Кинематографичный фон)
  // factor: 1.0f = без изменений, 0.0f = увести в кромешную тьму
  Pixel fx_darken(float factor) {
    r *= factor;
    g *= factor;
    b *= factor;

    return *this;
  }

  // 4. ТОНИРОВАНИЕ (Color Tinting под цвет темы)
  // tint: целевой цвет, в который окрашивается подложка (например, кремовый или темно-синий)
  // alpha: 0.0f = только картинка, 1.0f = полностью цвет заливки
  Pixel fx_tint(const Pixel tint, float alpha) {
      r = r * (1.0f - alpha) + tint.r * alpha;
      g = g * (1.0f - alpha) + tint.g * alpha;
      b = b * (1.0f - alpha) + tint.b * alpha;

      return *this;
  }
};

