#pragma once

// Перемикач режимів екрана: один активний Screen, перший доданий - основний.
//
//   setupScreens();               // з setup(), після setupDisplay()/setupTouchScreen()
//   screens.request("dino");      // з команди - лише запам'ятовує запит
//   screens.loop(frameStart);     // з loop(), усередині транзакції кадру
//   if (screens.active().realtime()) { ... }
//
// Чому request() не перемикає одразу: команди виконуються з-під транзакції
// кадру, а Arduino_HWSPI::beginWrite() на спільній шині (обидві C6-плати)
// робить SPI.beginTransaction() без обліку вкладеності - малювання звідти
// вішає плату намертво (docs/architecture.md, YIELD_DISPLAY_BUS). Тому
// перемикання й очищення робить loop().

#include <stddef.h>

#include <SerialCommander.hpp>

#include "Screen.hpp"

class ScreenManager {
public:
  static constexpr size_t kMaxScreens = 8;  // 4 екрани + запас

  // Перший доданий стає основним і активним.
  bool add(Screen& screen);

  // Перемкнутись на екран за іменем (з наступної ітерації loop()). false і
  // попередження в лог - якщо екрана немає або він зараз недоступний.
  bool request(const char* name);
  void requestHome();
  // Наступний доступний екран за порядком add() (по колу).
  void requestNext();

  // Очистити кадр (усі смуги) - після зміни сцени всередині екрана.
  void invalidate();

  // Застосувати запит, почистити смугу, якщо треба, і намалювати її.
  void loop(bool frameStart);

  Screen& active() { return *_active; }
  bool isHome() const { return _active == _screens[0]; }
  Screen* find(const char* name) const;

  size_t count() const { return _count; }
  Screen& at(size_t index) { return *_screens[index]; }

private:
  Screen* _screens[kMaxScreens] = {};
  size_t _count = 0;
  Screen* _active = nullptr;
  Screen* _pending = nullptr;
  uint8_t _pendingClear = 0;
};

extern ScreenManager screens;

// Реєструє всі екрани плати в screens і підписує кнопку й тач на маршрутизацію.
void setupScreens();

// Команда 'screen [list|next|home|<name>]'.
void registerScreenCommands(SerialCommander& commander);
