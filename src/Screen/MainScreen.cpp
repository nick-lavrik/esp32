#include "MainScreen.hpp"

#include <Logger.hpp>

#include "App/AppGlobals.hpp"
#include "BackgroundImages.hpp"
#include "ScreenControl.hpp"
#include "ScreenManager.hpp"

void MainScreen::drawStrip(bool frameStart) {
  (void)frameStart;
  drawBackgroundImage();
  drawSystemInfo();
  if (showClock) drawTime();
}

void MainScreen::onButtonRelease(uint32_t nowMs, uint32_t heldMs) {
  (void)nowMs;
  if (heldMs < 1000UL) show_clock(!showClock);
}

void MainScreen::onButtonLongPress(uint32_t nowMs) {
  (void)nowMs;
  if (display.brightness() == 0) {
    display_brightness(max(_savedBrightness, (uint8_t)1), _savedAutoBrightness);
  } else {
    _savedBrightness = display.brightness();
    _savedAutoBrightness = isAutoBrightness;
    display_brightness(0, false);
  }
}

void MainScreen::onHold(TouchPoint p, unsigned long heldMs) {
  (void)heldMs;
#if BOARD_HAS_LIGHT_SENSOR
  display_brightness(lightSensor.value(), true);
#endif
  // TODO: restore brightness before trigger autobrightness = off (!)
  // display.autobrightness(true);

  // Тип 2 (JobTask): "показувати frame"
  // постійно протягом 10 секунд, після чого само зникає з черги
  scheduler.addJob(
      10UL * 1000UL,
      [p]() {
        if (!screens.active().overlays()) return;  // інший екран - рамку не малюємо
        display.drawCircle(p.x, p.y, 4, TFT_YELLOW);
        display.drawRect(0, 0, 1, 1, TFT_WHITE);
        display.drawRect(display.width() - 1, 0, 1, 1, TFT_WHITE);
        display.drawRect(display.width() - 1, display.height() - 1, 1, 1, TFT_WHITE);
        display.drawRect(0, display.height() - 1, 1, 1, TFT_WHITE);

        display.drawRect(displayConfig.edgeZoneX, displayConfig.edgeZoneY,
                         displayConfig.screenWidth - 2 * displayConfig.edgeZoneX,
                         displayConfig.screenHeight - 2 * displayConfig.edgeZoneY, TFT_DARKGREY);
      },
      1  // з інтервалом 1 мілісекунда, а не на кожному tick()
  );

  Logger::info(" ------ !!! ONHOLD FRAME !!! ------ ");
}

void MainScreen::onSwipe(SwipeDirection dir, TouchPoint start, TouchPoint end) {
  (void)start;
  (void)end;
  // display_brightness(..., false) сам вимикає автояскравість і пише в NVS.
  if (dir == SwipeDirection::Up) {
    if (display.brightness() == 0) {
      display_brightness(1, false);
    } else if (display.brightness() == 1) {
      display_brightness(10, false);
    } else {
      display_brightness(min(100, display.brightness() + 10), false);
    }
    Logger::debug("Brightness: %d%% (increase)", display.brightness());
  } else if (dir == SwipeDirection::Down) {
    if (display.brightness() == 1) {
      display_brightness(0, false);
    } else {
      display_brightness(max(1, display.brightness() - 10), false);
    }
    Logger::debug("Brightness: %d%% (decrease)", display.brightness());
  }
}
