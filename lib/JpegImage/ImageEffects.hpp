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

  // gamma: 1.0f = без змін, >1.0f - світліше, <1.0f - темніше
  static bool applyGamma(JpegImage &image, float gamma);

  // levels: кількість рівнів на канал (>=2)
  static bool applyPosterize(JpegImage &image, int levels);

  // threshold: 0.0f..1.0f, типово 0.5f
  static bool applySolarize(JpegImage &image, float threshold = 0.5f);

  // Дуотон: інтерполяція dark..light за яскравістю пікселя (sepia - окремий випадок)
  static bool applyDuotone(JpegImage &image, const Pixel &dark, const Pixel &light);

  // rMul/gMul/bMul: 1.0f = без змін по каналу
  static bool applyColorBalance(JpegImage &image, float rMul, float gMul, float bMul);

  // amount: 0.0f = без змін, 1.0f = максимальний шум (±100% каналу)
  static bool applyNoise(JpegImage &image, float amount);

  // Впорядкований дизеринг (матриця Байєра 8x8) - прибирає смуги градієнта.
  // Кожен метод перевіряє, що `image.colorDepth()` відповідає його назві, і
  // повертає false інакше (не перепаковує піксель у "чужу" розрядність).
  static bool applyDitheringRGB332(JpegImage &image);
  static bool applyDitheringRGB565(JpegImage &image);
  static bool applyDitheringRGB888(JpegImage &image);

  // Box blur у 2 проходи (горизонтальний + вертикальний), з тимчасовим буфером
  // лише в один рядок/стовпець (не на весь кадр).
  // radius: 1-8 пікселів. passes: 2-3 дає результат, візуально близький до Гауса.
  static bool applyBoxBlur(JpegImage &image, uint8_t radius, uint8_t passes = 1);

  // Затемнення до країв кадру (радіальне, від центру). strength: 0.0f = без ефекту,
  // 1.0f = повне затемнення в кутах. Найдешевший з buffer-wide ефектів - без
  // тимчасового буфера, кожен піксель незалежний від сусідів.
  static bool applyVignette(JpegImage &image, float strength);

  // Усереднення blockSize x blockSize пікселів в один колір ("міняюча роздільність").
  // blockSize: 2 і більше.
  static bool applyPixelate(JpegImage &image, uint8_t blockSize);

  // Затемнення кожного парного рядка (ретро-CRT вигляд). darkenFactor: 1.0f = без
  // змін, 0.0f = парні рядки повністю чорні.
  static bool applyScanlines(JpegImage &image, float darkenFactor);

  // Хроматична аберація: зсуває R-канал вліво, B-канал вправо на offsetPx пікселів
  // (G лишається на місці). offsetPx: 1 і більше.
  static bool applyChromaticAberration(JpegImage &image, uint8_t offsetPx);

  // Виявлення країв (оператор Собеля), результат - Ч/Б "креслення". Потребує
  // мінімум 3x3 пікселі.
  static bool applySobelEdges(JpegImage &image);

  // Рельєфне тиснення (emboss), Ч/Б. strength: 1.0f = класична інтенсивність.
  // Потребує мінімум 3x3 пікселі.
  static bool applyEmboss(JpegImage &image, float strength = 1.0f);

 private:
  static Pixel getPixel(const JpegImage &image, size_t index);
  static void setPixel(JpegImage &image, size_t index, const Pixel &p);

  // Спільний прохід ordered-дизерингу: додає Bayer-шум заданої амплітуди (`spread`,
  // масштаб під квантування конкретної глибини кольору) перед запаковкою пікселя назад.
  // Викликач (applyDitheringRGB*) відповідає за перевірку colorDepth()/isLoaded().
  static void applyOrderedDither(JpegImage &image, float spread);

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
