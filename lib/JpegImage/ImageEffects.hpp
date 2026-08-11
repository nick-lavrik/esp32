#pragma once

#include <Arduino.h>

#include "JpegImage.hpp"
#include "Pixel.hpp"

// Використання:
//   ImageEffects::applyDesaturate(spaceImage, 0.3f);
//   ImageEffects::applyBoxBlur(spaceImage, /*radius=*/4, /*passes=*/2);
//
// Усі методи працюють IN-PLACE поверх буфера JpegImage - жоден метод не виділяє
// пам'яті під копію цілого кадру (виняток - applyBoxBlur, якому потрібен один
// тимчасовий рядок/стовпець, а не весь кадр, див. .cpp).
// Підтримувані глибини кольору: RGB332, RGB565, RGB888. MONO1 не підтримується
// (1 біт/піксель - кольорові трансформації для нього не мають сенсу) - методи
// повертають false і пишуть попередження в лог.
class ImageEffects {
 public:
  // factor: 0.0f = повна сірість, 1.0f = без змін
  static bool applyDesaturate(JpegImage &image, float factor);

  // factor: 0.0f = без змін, 1.0f = у білий
  static bool applyLighten(JpegImage &image, float factor);

  // factor: 1.0f = без змін, 0.0f = у чорний
  static bool applyDarken(JpegImage &image, float factor);

  // alpha: 0.0f = без змін, 1.0f = повністю колір tint
  static bool applyTint(JpegImage &image, const Pixel &tint, float alpha);

  // contrast: 1.0f = без змін, >1.0f - підвищення контрасту
  static bool applyContrast(JpegImage &image, float contrast);

  // amount: 0.0f = без змін, 1.0f = повна сепія
  static bool applySepia(JpegImage &image, float amount = 1.0f);

  // angleRad: 0 .. 2*PI
  static bool applyHueRotate(JpegImage &image, float angleRad);

  static bool applyThermal(JpegImage &image);
  static bool applyInvert(JpegImage &image);

  // threshold: 0.0f..1.0f, типово 0.5f
  static bool applyThreshold(JpegImage &image, float threshold = 0.5f);

  // Впорядкований дизеринг (матриця Байєра 8x8) - прибирає смуги градієнта.
  // Має сенс лише для RGB332 (низька розрядність каналів); для інших глибин
  // повертає false.
  static bool applyDithering(JpegImage &image);

  // Box blur у 2 проходи (горизонтальний + вертикальний), з тимчасовим буфером
  // лише в один рядок/стовпець (не на весь кадр).
  // radius: 1-8 пікселів. passes: 2-3 дає результат, візуально близький до Гауса.
  static bool applyBoxBlur(JpegImage &image, uint8_t radius, uint8_t passes = 1);

 private:
  static Pixel getPixel(const JpegImage &image, size_t index);
  static void setPixel(JpegImage &image, size_t index, const Pixel &p);

  template <typename Fn>
  static bool applyPerPixel(JpegImage &image, Fn &&fn) {
    if (image.colorDepth() == JpegColorDepth::MONO1) {
      return false;
    }
    if (!image.isLoaded()) {
      return false;
    }
    size_t count = (size_t)image.width() * image.height();
    for (size_t i = 0; i < count; i++) {
      Pixel p = getPixel(image, i);
      fn(p);
      setPixel(image, i, p);
    }
    return true;
  }
};
