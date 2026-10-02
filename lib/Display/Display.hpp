// Display.hpp
#pragma once

#include <stdarg.h>  // Обов'язково для роботи з трикрапкою (...)

#include <TLogger.hpp>

// Display - логіка кадру поверх драйвера: смуги (DISPLAY_SPLIT_COUNT),
// зсув і відсікання по активній смузі, дзеркало екрана, масштабування
// спрайтів. Сам не знає ні графічної бібліотеки, ні BOARD_* - усе це
// всередині DisplayDriver (DisplayDriver.hpp), який передається в
// конструктор.
//
// HAS_SCREEN_MIRROR (дзеркало екрана у веб-порталі, lib/ScreenMirror) -
// похідний прапорець з DISPLAY_SPLIT_COUNT/SPRITE_COLOR_DEPTH, обчислюється
// в одному місці - include/features.h (поруч із BOARD_HAS_LIGHT_SENSOR, той
// самий підхід), а не тут.
#include "features.h"

#include "DisplayDriver.hpp"
#include "Rgb332.hpp"  // rgb332to565() - тим, хто малює через Display (test-gfx)

class Display {
public:
  explicit Display(DisplayDriver& driver) : driver_(driver) {}

  void startWrite() {
    #if DISPLAY_SPLIT_COUNT > 0
    _activeSplitBlock = (_activeSplitBlock + 1) % DISPLAY_SPLIT_COUNT;
    // _logger.debug("active split block = %d", _activeSplitBlock); delay(600);
    #endif
    driver_.startWrite();
    _writing = true;
  }

  void endWrite() {
    driver_.endWrite();
    _writing = false;
  }

  // Чи відкрита зараз транзакція шини дисплея.
  //
  // Потрібно тим, хто збирається звернутися до шини з-під невідомого
  // контексту: loop() тримає транзакцію відкритою через увесь кадр, але
  // ті самі функції викликаються і поза нею (напр. display_flip() -
  // з консольної команди всередині кадру, а з updateImuFlip() вже після
  // endWrite()). Дужка YIELD_DISPLAY_BUS() у src/main.cpp питає саме це,
  // щоб знати, чи треба потім ВІДНОВЛЮВАТИ транзакцію: безумовне
  // відновлення залишило б її відкритою там, де її не було, і наступний
  // startWrite() у loop() дав би дедлок.
  bool isWriting() const { return _writing; }

  // Відпустити/відновити шину, НЕ чіпаючи isWriting(): стан "кадр
  // малюється" лишається, віддається лише сама транзакція (YIELD_DISPLAY_BUS()
  // у src/main.cpp). startWrite()/endWrite() для цього не годяться - перший
  // просуває смугу, другий скидає _writing.
  void releaseBus() { driver_.endWrite(); }
  void reacquireBus() { driver_.startWrite(); }

  // Скільки ітерацій loop() складають ОДИН повний кадр.
  // При вимкненому спліті (esp32-c3, DISPLAY_SPLIT_COUNT=0) кадр збирається
  // за одну ітерацію, тому 1, а не 0 - на це значення діляться.
  static constexpr uint8_t splitCount() {
#if DISPLAY_SPLIT_COUNT > 0
    return (uint8_t)DISPLAY_SPLIT_COUNT;
#else
    return 1;
#endif
  }

  // Висота однієї смуги (== height() у небуферизованому режимі).
  int splitHeight() const { return height() / splitCount(); }

  // Індекс смуги, яку малює ПОТОЧНА ітерація loop() (0 = верхня, y == 0).
  // Поле _activeSplitBlock існує лише при DISPLAY_SPLIT_COUNT > 0 - звідси #if,
  // а не звичайний геттер.
  uint8_t splitIndex() const {
#if DISPLAY_SPLIT_COUNT > 0
    return (uint8_t)_activeSplitBlock;
#else
    return 0;
#endif
  }

  // true рівно раз на повний кадр - на смузі 0, тобто на ВЕРХНІЙ.
  //
  // Хто змінює сцену між кадрами (напр. ігрова фізика), мусить робити це
  // ТІЛЬКИ тут: тоді смуги 0..N-1 малюються з одним станом світу і кадр
  // збирається як один цілісний екран згори вниз. Оновлення щоітерації дало б
  // кожній смузі свою фазу руху - те саме "розривання" на межах смуг, про яке
  // попереджає docs/architecture.md у розділі про DISPLAY_SPLIT_COUNT.
  //
  // Викликати ПІСЛЯ startWrite() (саме він просуває _activeSplitBlock).
  bool isFrameStart() const { return splitIndex() == 0; }

  void flip();

  uint8_t getRotation() const { return driver_.getRotation(); }
  void setRotation(uint8_t r) { driver_.setRotation(r); }

  // Ініціалізація дисплея (обов'язково викликати в setup())
  void init();

  // Заливка всього екрану кольором
  void clear(uint16_t color = TFT_BLACK) { driver_.fillScreen(color); }

  // Малювання тексту в позиції (x, y)
  void drawText(int x, int y, const char *text, uint16_t color);

  // Виводить накопичений у спрайті кадр на реальний екран.
  // Викликати після того, як усе малювання кадру завершено.
  void flush();

  uint8_t brightness() { return brightness_; }  // percent!
  void brightness(uint8_t percent);

  void pushImage(int32_t x, int32_t y, int32_t w, int32_t h, const uint16_t *data) {
    dXY(&x, &y);
    driver_.pushImage(x, y, w, h, data);
  }
  // RGB332 (8bpp) - як саме його малювати, вирішує драйвер (див.
  // TftEspiDriver::pushImage8bpp()).
  void pushImage8bpp(int32_t x, int32_t y, int32_t w, int32_t h, const uint8_t *data);
  void setCursor(int32_t x, int32_t y) { dXY(&x, &y); driver_.setCursor(x, y); }

  // Ширина/висота активної області екрану (з урахуванням rotation)
  int width() const { return width_; }
  int height() const { return height_; }

  // Без const на типі повернення: для скаляра він не має сенсу й ігнорується
  // компілятором (-Wignored-qualifiers).
  uint32_t loopFrameRate();
  size_t fontHeight() { return driver_.fontHeight(); }

  void setTextFont(uint8_t f) { driver_.setTextFont(f); }
  void setTextColor(uint16_t color) { driver_.setTextColor(color); }
  void setTextColor(uint16_t color, uint16_t bg) { driver_.setTextColor(color, bg); }
  void setTextSize(uint8_t size) { driver_.setTextSize(size); }

  void drawRect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color) {
    dXY(&x, &y); driver_.drawRect(x, y, w, h, color);
  }

  void drawCircle(int32_t x, int32_t y, int32_t r, uint32_t color) {
    dXY(&x, &y);
    driver_.drawCircle(x, y, r, color);
  }

  void fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color) {
    dXY(&x, &y);
    if (y >= splitHeight() || y + h <= 0) return;  // цілком поза активною смугою
    driver_.fillRect(x, y, w, h, color);
  }

  // Цілочисельне масштабування 1bpp-спрайта (nearest neighbour - тобто точний
  // піксель-арт, без згладжування). scale==1 еквівалентний drawBitmap().
  // Потрібне тому, що ні TFT_eSPI, ні Adafruit_GFX не вміють масштабувати
  // drawBitmap, а на 480x480 спрайт розміром 47 px виглядав би мурахою.
  void drawBitmapScaled(int32_t x, int32_t y, const uint8_t *bitmap, int32_t w, int32_t h,
                        uint32_t color, uint8_t scale);

  int16_t textWidth(const char *string) { return driver_.textWidth(string); }

  size_t print(const char *string) { return driver_.print(string); }
  size_t println(const char *string) { return driver_.println(string); }

  // Ранній вихід тут не косметика: drawBitmap на ОБОХ бекендах (TFT_eSPI і
  // Arduino_GFX) відсікає ПО ПІКСЕЛЮ, тобто чесно проганяє цикл w*h навіть
  // коли спрайт цілком за межами смуги. При DISPLAY_SPLIT_COUNT=6 це шестикратна
  // робота "в нікуди" на кожен об'єкт сцени.
  void drawBitmap( int16_t x, int16_t y, const uint8_t *bitmap, int16_t w, int16_t h, uint16_t fgcolor) {
    dXY(&x, &y);
    if (y >= (int16_t)splitHeight() || y + h <= 0) return;
    driver_.drawBitmap(x, y, bitmap, w, h, fgcolor);
  };

  template <typename T>
  void dXY(T* x, T* y) {
    (void)x;
    #if DISPLAY_SPLIT_COUNT > 0
    *y = *y - static_cast<T>(_activeSplitBlock * splitHeight());
    #else
    (void)y;
    #endif
  }

  template <typename... Args>
  size_t printf(const __FlashStringHelper *ifsh, const Args &...args) {
    return driver_.printf((PGM_P)ifsh, args...);
  }

  template <typename... Args>
  size_t printf(const char *format, const Args &...args) {
    return driver_.printf(format, args...);
  }

private:
  DisplayDriver& driver_;
  bool _writing = false;  // стан транзакції шини (див. isWriting())
#if DISPLAY_SPLIT_COUNT > 0
  int8_t _activeSplitBlock = 0;
#endif

  int width_ = 0;
  int height_ = 0;
  uint8_t brightness_ = 50;  // percent!

  const TLogger _logger{"tft"};
};
