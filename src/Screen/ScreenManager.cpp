#include "ScreenManager.hpp"

#include <string.h>

#include <Logger.hpp>
#include <TLogger.hpp>

#include "App/AppGlobals.hpp"
#include "DinoScreen.hpp"
#include "DinoSpritesScreen.hpp"
#include "MainScreen.hpp"
#include "TestGfxScreen.hpp"

ScreenManager screens;

namespace {
const TLogger logger{"screen"};

MainScreen mainScreen;
#if HAS_DINO_GAME
DinoScreen dinoScreen;
DinoSpritesScreen dinoSpritesScreen;
#endif
}  // namespace

const char* buttonIdName(ButtonId id) { return id == ButtonId::Primary ? "primary" : "secondary"; }

void Screen::onButton(ButtonId id, ButtonEvent event, uint32_t nowMs, uint32_t heldMs) {
  (void)id;
  (void)nowMs;
  (void)heldMs;
  if (event == ButtonEvent::DoubleClick) {
    screens.requestNext();
  } else if (event == ButtonEvent::LongPress) {
    screens.requestHome();
  }
}

bool ScreenManager::add(Screen& screen) {
  if (_count >= kMaxScreens) {
    logger.error("no room for screen '%s' (kMaxScreens=%u)", screen.name(), (unsigned)kMaxScreens);
    return false;
  }
  _screens[_count++] = &screen;
  if (_active == nullptr) _active = &screen;
  return true;
}

Screen* ScreenManager::find(const char* name) const {
  for (size_t i = 0; i < _count; ++i) {
    if (!strcmp(_screens[i]->name(), name)) return _screens[i];
  }
  return nullptr;
}

bool ScreenManager::request(const char* name) {
  Screen* target = find(name);
  if (target == nullptr) {
    logger.warn("unknown screen '%s' (see 'screen list')", name);
    return false;
  }
  if (!target->available()) {
    logger.warn("screen '%s' is not available", name);
    return false;
  }
  // Запит на вже активний екран - не перемикання: enter()/leave() не кличемо.
  _pending = (target == _active) ? nullptr : target;
  return true;
}

void ScreenManager::requestHome() { request(_screens[0]->name()); }

void ScreenManager::requestNext() {
  const Screen* current = _pending != nullptr ? _pending : _active;
  size_t index = 0;
  while (index < _count && _screens[index] != current) ++index;
  for (size_t step = 1; step <= _count; ++step) {
    Screen* candidate = _screens[(index + step) % _count];
    if (candidate->available()) {
      request(candidate->name());
      return;
    }
  }
}

void ScreenManager::invalidate() { _pendingClear = display.splitCount(); }

void ScreenManager::loop(bool frameStart) {
  if (_pending != nullptr) {
    _active->leave();
    _active = _pending;
    _pending = nullptr;
    _active->enter();
    // Кадр збирається за splitCount() проходів loop(), тому одного clear()
    // не досить: чистимо стільки ж смуг. Інакше на платах, де height() не
    // ділиться на DISPLAY_SPLIT_COUNT рівно, шматки старої картинки лишились
    // би на екрані назавжди.
    invalidate();
    logger.info("-> %s", _active->name());
  }
  _active->update(frameStart);
  if (_pendingClear) {
    display.clear();
    --_pendingClear;
  }
  _active->drawStrip(frameStart);
}

void setupScreens() {
  screens.add(mainScreen);
#if HAS_DINO_GAME
  setupDinoGame();
  screens.add(dinoScreen);
  screens.add(dinoSpritesScreen);
#endif
  screens.add(testGfxScreen());

#if BOARD_HAS_TOUCHSCREEN
  // Одна підписка на подію на весь застосунок - далі вирішує активний екран.
  TouchEvents& events = touchController.events();
  events.onTouch([](TouchPoint p) { screens.active().onTouch(p); });
  events.onRelease([](TouchPoint p) { screens.active().onRelease(p); });
  events.onHold([](TouchPoint p, unsigned long ms) { screens.active().onHold(p, ms); });
  events.onSwipeUp([](TouchPoint s, TouchPoint e) { screens.active().onSwipe(SwipeDirection::Up, s, e); });
  events.onSwipeDown([](TouchPoint s, TouchPoint e) { screens.active().onSwipe(SwipeDirection::Down, s, e); });
  events.onSwipeLeft([](TouchPoint s, TouchPoint e) { screens.active().onSwipe(SwipeDirection::Left, s, e); });
  events.onSwipeRight([](TouchPoint s, TouchPoint e) { screens.active().onSwipe(SwipeDirection::Right, s, e); });
#endif

  logger.info("%u screen(s), active '%s'", (unsigned)screens.count(), screens.active().name());
}

void registerScreenCommands(SerialCommander& commander) {
  commander.registerCommand("screen", "display mode: screen [list|next|home|<name>]", [](const String& args) {
    if (args.equalsIgnoreCase("next")) {
      screens.requestNext();
    } else if (args.equalsIgnoreCase("home")) {
      screens.requestHome();
    } else if (args.length() != 0 && !args.equalsIgnoreCase("list")) {
      screens.request(args.c_str());
    } else {
      // Окремий тег: таблиця має іншу форму, ніж повторюваний рядок "-> <name>".
      // Не static: команда рідкісна, а static TLogger - 148 Б .bss назавжди.
      const TLogger listLog{"screens"};
      for (size_t i = 0; i < screens.count(); ++i) {
        Screen& s = screens.at(i);
        listLog.info("%c %-14s%s", &s == &screens.active() ? '*' : ' ', s.name(),
                     s.available() ? "" : " (not available)");
      }
    }
  });
}
