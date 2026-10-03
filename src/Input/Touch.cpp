// Тачскрін (BOARD_HAS_TOUCHSCREEN): контролер і verbose-логи жестів. Що
// жест ОЗНАЧАЄ, вирішує активний екран (src/Screen/ScreenManager.cpp).

#include <Arduino.h>

#include <Logger.hpp>
#include <TLogger.hpp>

#include "App/AppGlobals.hpp"
#include "Input.hpp"

#if BOARD_HAS_TOUCHSCREEN
// Увесь тач логується у verbose під власним тегом. Не в debug: типовий рівень
// DEFAULT_LOG_LEVEL=3 - це саме Debug (LogLevel, JournalEntry.hpp), тобто debug
// ішов у консоль і MQTT на кожну подію, а сирі координати з TouchEvents::update()
// - на кожне опитування, поки палець лежить. Щоб побачити дотики під час
// налагодження: 'journal level touch verbose'.
//
// Раніше цього не вміли, тому існувала окрема команда 'touchlog on|off' і
// глобальний прапорець, що піднімав рівень одного повідомлення до info. І
// команда, і прапорець зникли: керування рівнем за тегом тепер спільне.
static const TLogger touchLog{"touch"};

// Підписаний на onTouch (момент НАТИСКАННЯ), а не на onClick: для перевірки
// «чи взагалі бачить панель і чи не з'їхав мапер» потрібен кожен дотик, тоді
// як onClick мовчить, якщо жест виявився свайпом або переріс у hold - саме в
// тих випадках, коли причину й шукають.
void onTouchLog(TouchPoint p) { touchLog.verbose("Touch: %d, %d", p.x, p.y); }
void onHoldHandler(TouchPoint p, unsigned long ms) { touchLog.verbose("Hold at %d,%d for %lu ms", p.x, p.y, ms); }
void onDblClickHandler(TouchPoint p) { touchLog.verbose("Double click: %d, %d\n", p.x, p.y); }

void onSwipeLeftHandler(TouchPoint start, TouchPoint end) { touchLog.verbose("Swipe LEFT"); }
void onSwipeRightHandler(TouchPoint start, TouchPoint end) { touchLog.verbose("Swipe RIGHT"); }
void onSwipeUpHandler(TouchPoint start, TouchPoint end) { touchLog.verbose("Swipe UP"); }
void onSwipeDownHandler(TouchPoint start, TouchPoint end) { touchLog.verbose("Swipe DOWN"); }

void onSwipeFromBottomHandler(TouchPoint start, TouchPoint end) {
  touchLog.verbose("Swipe FROM BOTTOM (e.g. open menu)");
}
void onSwipeFromTopHandler(TouchPoint start, TouchPoint end) {
  touchLog.verbose("Swipe FROM TOP (e.g. notification shade)");
}
void onSwipeFromLeftHandler(TouchPoint start, TouchPoint end) { touchLog.verbose("Swipe FROM LEFT (e.g. back)"); }
void onSwipeFromRightHandler(TouchPoint start, TouchPoint end) {
  touchLog.verbose("Swipe FROM RIGHT (e.g. side panel)");
}

#endif

void setupTouchScreen() {
#if BOARD_HAS_TOUCHSCREEN
  touch.setTouchPointMapper(&mapper);
  touchController.setup(&touch);
  Logger::debug("TouchScreen setup done");

  touchController.events().onTouch(onTouchLog);
  touchController.events().onHold(onHoldHandler);
  touchController.events().onDblClick(onDblClickHandler);
  touchController.events().onSwipeLeft(onSwipeLeftHandler);
  touchController.events().onSwipeRight(onSwipeRightHandler);
  touchController.events().onSwipeUp(onSwipeUpHandler);
  touchController.events().onSwipeDown(onSwipeDownHandler);
  touchController.events().onSwipeFromBottom(onSwipeFromBottomHandler);
  touchController.events().onSwipeFromTop(onSwipeFromTopHandler);
  touchController.events().onSwipeFromLeft(onSwipeFromLeftHandler);
  touchController.events().onSwipeFromRight(onSwipeFromRightHandler);

  Logger::info("TouchScreen controller done");
#else
  Logger::info("TouchScreen not found (disabled)!");
#endif
}
