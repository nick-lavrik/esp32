// Кнопки (PRIMARY_BUTTON_PIN; SECONDARY_BUTTON_PIN - див. нижче): виявлення
// натиску/утримання/відпускання; що вони
// означають, вирішує активний екран (Screen::onButton*()).
//
// SECONDARY_BUTTON_PIN (ttgo-t1) поки лише описаний в environment.h: друга
// кнопка з'явиться разом із ButtonEvents (click / double-click / long press,
// docs/tech_debt.md) - як другий екземпляр того самого класу, а не друга
// копія цієї cron-логіки.

#include <Arduino.h>

#include <Logger.hpp>

#include "App/AppGlobals.hpp"
#include "Input.hpp"
#include "Screen/ScreenManager.hpp"

void setupButtons() {
#if defined(PRIMARY_BUTTON_PIN)
  pinMode(PRIMARY_BUTTON_PIN, INPUT_PULLUP);  // BOOT-кнопка: замикає на GND
  // Тут лише ВИЯВЛЕННЯ подій (натиск, утримання 3 с, відпускання); що вони
  // означають, вирішує активний екран (src/Screen/Screen.hpp, onButton*()).
  // Друга cron-задача на той самий пін не годиться: обидві читали б
  // digitalRead і кожна рахувала б свій фронт. Поріг 3 с ігрове утримання не
  // досягає (holdExtraSec у грі - 0.18 с), тож "вихід з гри" зі стрибком не
  // конфліктує.
  scheduler.addCronTask(0, []() -> void {
    static bool pressed = false;
    static bool longFired = false;
    static uint32_t pressedTs = 0;
    const uint32_t now = millis();
    const bool down = digitalRead(PRIMARY_BUTTON_PIN) == LOW;

    if (down && !pressed) {
      pressed = true;
      longFired = false;
      pressedTs = now;
      screens.active().onButtonPress(now);
      Logger::info("Button pressed!");
    } else if (down && !longFired && now - pressedTs > 3000UL) {
      longFired = true;
      screens.active().onButtonLongPress(now);
    } else if (!down && pressed) {
      pressed = false;
      screens.active().onButtonRelease(now, now - pressedTs);
      Logger::info("Button released!");
    }
  });
  Logger::info("buttons: primary=GPIO%d", PRIMARY_BUTTON_PIN);
#endif
}
