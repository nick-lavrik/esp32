// Setup_Headless.h
// TFT_eSPI-сумісна ЗАГЛУШКА для плат БЕЗ дисплея (BOARD_HAS_DISPLAY=0).
//
// НАВІЩО: спільний прикладний код проєкту (src/main.cpp, src/Display.*,
// src/ntp.h, src/setup.h, src/BackgroundImages.*) написаний під API
// TFT_eSPI і кличе display.*/tft.* у сотнях місць. Обвішувати їх усі
// "#if BOARD_HAS_DISPLAY" - це кілька сотень правок у файлі на 4000+
// рядків, з ризиком розсинхронити гілки при кожній наступній зміні.
//
// Замість цього тут - той самий прийом, що вже застосований для
// SSD1306 (Setup_SSD1306_NodeMCU.h) та Arduino_GFX (Setup_JD9853_C6.h):
// клас із потрібною ПІДМНОЖИНОЮ API TFT_eSPI, але з порожніми тілами.
// Компілятор викидає ці виклики повністю (усе inline і no-op), тобто
// у прошивці від "дисплея" не лишається ні коду, ні даних, і жодна
// графічна бібліотека (TFT_eSPI / Arduino_GFX / LovyanGFX) у lib_deps
// не потрібна.
//
// Підключається через src/TftInstance.h за !BOARD_HAS_DISPLAY - ця
// перевірка стоїть ПЕРШОЮ, тобто перекриває будь-який BOARD_*.
//
// width()/height() повертають 0: TFT_WIDTH/TFT_HEIGHT на такій платі
// теж 0 (див. build_flags), і весь код розкладки лишається арифметично
// коректним, просто нічого не малює.

#pragma once

#include <Arduino.h>

// Кольори. Реальних значень тут не потрібно (нічого не малюється), але
// макроси мають існувати - прикладний код передає їх у drawText/clear/
// setTextColor. Значення взяті "як у TFT_eSPI" рівно для того, щоб при
// вмиканні дисплея на цій платі нічого не довелось переписувати.
#define TFT_BLACK 0x0000
#define TFT_WHITE 0xFFFF
#define TFT_RED 0xF800
#define TFT_GREEN 0x07E0
#define TFT_DARKGREEN 0x03E0
#define TFT_YELLOW 0xFFE0
#define TFT_CYAN 0x07FF
#define TFT_MAGENTA 0xF81F
#define TFT_ORANGE 0xFDA0
#define TFT_LIGHTGREY 0xD69A
#define TFT_DARKGREY 0x7BEF
#define TFT_TRANSPARENT 0x0120

// Датуми тексту (підмножина TFT_eSPI, якої вистачає src/Display.cpp)
#define TL_DATUM 0
#define MC_DATUM 4

// Успадкування від Print дає безкоштовно print()/println()/printf() -
// рівно ті сигнатури, які кличе src/Display.h. write() відкидає байти,
// тому ці виклики нічого не коштують, крім форматування рядка.
//
// ВАЖЛИВО: TFT_CS / TFT_DC / TFT_RST / TFT_BL тут НЕ визначені навмисно.
// src/main.cpp і src/Display.cpp перевіряють їх через "#if defined(...)",
// тобто без цих макросів уся робота з пінами дисплея (pinMode, підсвітка,
// резервування пінів у gpio-командах) сама собою зникає зі збірки.
class TFT_eSPI : public Print {
public:
  TFT_eSPI() = default;

  void init() {}

  size_t write(uint8_t) override { return 1; }
  size_t write(const uint8_t*, size_t size) override { return size; }

  void setRotation(uint8_t r) { _rotation = r; }
  uint8_t getRotation() const { return _rotation; }

  int16_t width() const { return 0; }
  int16_t height() const { return 0; }

  // Транзакції шини (див. YIELD_DISPLAY_BUS у src/main.cpp) - шини немає
  void startWrite() {}
  void endWrite() {}

  void fillScreen(uint16_t) {}
  void setSwapBytes(bool) {}

  void setCursor(int32_t, int32_t) {}
  void setTextFont(uint8_t) {}
  void setTextSize(uint8_t) {}
  void setTextColor(uint16_t) {}
  void setTextColor(uint16_t, uint16_t) {}
  void setTextDatum(uint8_t) {}

  size_t fontHeight() { return 0; }
  int16_t textWidth(const char*) { return 0; }
  uint16_t drawString(const char*, int32_t, int32_t) { return 0; }

  void drawRect(int32_t, int32_t, int32_t, int32_t, uint32_t) {}
  void drawCircle(int32_t, int32_t, int32_t, uint32_t) {}
  void drawBitmap(int16_t, int16_t, const uint8_t*, int16_t, int16_t, uint16_t) {}

  // Обидві перевантаження pushImage() з src/Display.cpp: RGB565 і
  // "рідний" 8bpp (RGB332) варіант bodmer/TFT_eSPI.
  void pushImage(int32_t, int32_t, int32_t, int32_t, const uint16_t*) {}
  void pushImage(int32_t, int32_t, int32_t, int32_t, uint8_t*, bool, uint16_t* = nullptr) {}

private:
  uint8_t _rotation = 0;
};

/**
 * єдиний глобальний екземпляр, визначений в
 * @see file://./../src-esp32-c3/TftInstance.cpp
 */
extern TFT_eSPI tft;

// Спрайт на платі без дисплея не потрібен: env з BOARD_HAS_DISPLAY=0
// зобов'язаний мати DISPLAY_SPLIT_COUNT=0, і тоді src/Display.h бере
// гілку "sprite() == tft_" (без буфера кадру). Клас лишено як alias
// суто щоб згадка TFT_eSprite деінде не ламала збірку.
using TFT_eSprite = TFT_eSPI;
