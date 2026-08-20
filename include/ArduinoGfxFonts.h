#pragma once

// Шар сумісності "номер шрифту TFT_eSPI -> шрифт Arduino_GFX".
//
// НАВІЩО: спільний src/main.cpp написаний під API TFT_eSPI і викликає
// display.setTextFont(N). На платах з Arduino_GFX (обидві C6) справжнього
// TFT_eSPI немає, тому прапорці LOAD_FONT* самі по собі не роблять НІЧОГО -
// їх читає лише bodmer/TFT_eSPI. Раніше вони й стояли в build_flags
// esp32-c6-lcd096 як мертвий вантаж, а setTextFont() був порожньою
// заглушкою.
//
// Тут ті самі прапорці отримують справжній сенс: кожен LOAD_FONT<N>=1
// підключає конкретний шрифт і додає його в таблицю нижче. Вимкнений
// прапорець означає, що масив шрифту навіть не потрапляє у прошивку -
// саме та поведінка, що й у TFT_eSPI (економія flash).
//
// ВІДПОВІДНІСТЬ НОМЕРІВ (наближена - точних копій шрифтів TFT_eSPI немає):
//   1 / LOAD_GLCD  - вбудований 5x7 Adafruit (Arduino_GFX має його завжди)
//   2 / LOAD_FONT2 - FreeSans9pt7b        (~16 px, пропорційний)
//   4 / LOAD_FONT4 - FreeSansBold12pt7b   (~26 px, пропорційний, жирний)
//   7 / LOAD_FONT7 - u8g2 7Segments 26x42 (~42 px, ЛИШЕ цифри та : - .)
//
// НЕ ПІДКЛЮЧЕНО: 6 і 8 (великі цифрові шрифти TFT_eSPI). Каркас під них
// готовий - додати файл шрифту в include/fonts/, ще один #if нижче і
// рядок у таблиці; окремої логіки не потрібно.

#include <Arduino_GFX_Library.h>

#if defined(LOAD_FONT2) && LOAD_FONT2
#include "fonts/FreeSans9pt7b.h"
#endif

#if defined(LOAD_FONT4) && LOAD_FONT4
#include "fonts/FreeSansBold12pt7b.h"
#endif

#if defined(LOAD_FONT7) && LOAD_FONT7
#include "fonts/u8g2_font_7Segments_26x42_mn.h"
#endif

namespace ArduinoGfxFonts {

// Опис одного шрифту. Рівно один з gfx/u8g2 ненульовий; обидва nullptr -
// вбудований 5x7.
struct FontEntry {
  const GFXfont* gfx;    // Adafruit GFXfont
  const uint8_t* u8g2;   // u8g2-формат (потребує setUTF8Print(true))
  uint8_t height;        // висота рядка в пікселях при textSize=1
};

// Висоти задані таблицею, а не обчислюються через getTextBounds() щоразу.
// Причина: fontHeight() у src/main.cpp викликається десятки разів на кадр
// для розкладки рядків (row * (space + fontHeight())), іганяти на кожен
// виклик обхід гліфів - марна трата такту. Значення взяті з yAdvance
// відповідних шрифтів.
inline FontEntry lookup(uint8_t font) {
  switch (font) {
#if defined(LOAD_FONT2) && LOAD_FONT2
    case 2: return {&FreeSans9pt7b, nullptr, 22};
#endif
#if defined(LOAD_FONT4) && LOAD_FONT4
    case 4: return {&FreeSansBold12pt7b, nullptr, 29};
#endif
#if defined(LOAD_FONT7) && LOAD_FONT7
    case 7: return {nullptr, u8g2_font_7Segments_26x42_mn, 42};
#endif
    default:
      // Сюди потрапляє і font==1 (GLCD), і будь-який номер, чий LOAD_FONT*
      // вимкнено. Мовчазний відкат на вбудований 5x7 - навмисний: краще
      // дрібний текст, ніж порожній екран, а невідповідність одразу видно
      // на дисплеї.
      return {nullptr, nullptr, 8};
  }
}

// Застосовує шрифт до будь-якого об'єкта Arduino_GFX (дисплей або канва).
// Повертає висоту рядка, щоб викликач одразу закешував її для fontHeight().
template <typename GfxT>
inline uint8_t apply(GfxT* gfx, uint8_t font) {
  const FontEntry e = lookup(font);

  if (e.u8g2) {
    gfx->setFont(e.u8g2);
    gfx->setUTF8Print(true);
  } else {
    // Вимкнути UTF8Print ОБОВ'ЯЗКОВО: інакше після переходу з u8g2-шрифта
    // назад на GFXfont друк лишається в UTF-8 режимі і байти тексту
    // інтерпретуються як мультибайтні послідовності.
    gfx->setUTF8Print(false);
    gfx->setFont(e.gfx);  // nullptr = вбудований 5x7
  }
  return e.height;
}

}  // namespace ArduinoGfxFonts
