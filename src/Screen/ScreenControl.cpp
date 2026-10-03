#include "ScreenControl.hpp"

#include <ConfigStorage.hpp>
#include <Logger.hpp>

#include "App/AppGlobals.hpp"
#include "DisplayBusYield.hpp"

bool showClock = true;
bool isAutoBrightness = false;

// Застосувати яскравість БЕЗ запису в NVS.
void display_brightness_apply(uint8_t percent, bool _auto) {
  display.brightness(percent);
  isAutoBrightness = _auto;
}

// Застосувати ТА зберегти в NVS. Викликати лише для явних дій користувача
// (команда, свайп, кнопка).
//
// В авто-режимі значення змінюється на кожну зміну показань сенсора (гістерезис
// 5%), і раніше кожна з них давала ДВА записи в NVS - це пряме зношування flash
// (у NVS обмежена кількість циклів стирання). Зберігати там нічого й не
// потрібно: на старті яскравість в авто-режимі однаково перераховується з
// сенсора. Тому слухач сенсора користується display_brightness_apply().
void display_brightness(uint8_t percent, bool _auto) {
  display_brightness_apply(percent, _auto);
  configStorage.setInt(CFG_DISPLAY_BRIGHTNESS, display.brightness());
  configStorage.setBool(CFG_SYS_AUTOBRIGHTNESS, isAutoBrightness);
  Logger::info("display.brightness(%d)%s", display.brightness(), isAutoBrightness ? " (auto)" : "");
}

void display_flip() {
  // setRotation() усередині Arduino_GFX сам відкриває транзакцію шини -
  // без цієї дужки виклик з консольної команди (тобто з-під кадру) вішав
  // плату намертво, без шансу на watchdog.
  YIELD_DISPLAY_BUS();

  displayConfig.invertY = !displayConfig.invertY;
  displayConfig.invertX = !displayConfig.invertX;
  display.flip();
}

void show_clock(bool show) {
  configStorage.setBool(CFG_SHOW_CLOCK, showClock = show);
  Logger::debug("showClock = %s", showClock ? "YES" : "NO");
}

void registerScreenControlCommands(SerialCommander& commander) {
  commander.registerCommand("flip", "flip display (180)", [](const String& args) { display_flip(); });

  commander.registerCommand("clock", "show hide clock on screen: clock on|off", [](const String& args) {
    if (args.equalsIgnoreCase("on")) {
      show_clock(true);
    } else if (args.equalsIgnoreCase("off")) {
      show_clock(false);
    } else {
      Logger::info("use: clock on|off");
    }
  });

  commander.registerCommand("brightness", "control screen brightness: brightness 0-100|auto", [](const String& args) {
    if (args.length() == 0) {
      Logger::info("use: brightness 0-100|auto");
    } else if (args.equalsIgnoreCase("auto")) {
#if BOARD_HAS_LIGHT_SENSOR
      display_brightness(lightSensor.value(), true);
      Logger::info(" isAutoBrighness = %s", isAutoBrightness ? "true" : "false");
#else
      Logger::info(" isAutoBrighness **disabled**");
#endif
    } else if (args.toInt() < 0 || args.toInt() > 100 || (args.toInt() == 0 && args != "0")) {
      // Обрізати до 100 мовчки не можна: uint8_t-параметр перетворив би 300
      // на 44, а "abc" (toInt() == 0) погасив би екран.
      Logger::warn("use: brightness 0-100|auto");
    } else {
      display_brightness(args.toInt(), false);
    }
  });
}

void loadScreenSettings() {
  showClock = configStorage.getBool(CFG_SHOW_CLOCK, true);
  isAutoBrightness = configStorage.getBool(CFG_SYS_AUTOBRIGHTNESS, false);
  // Ігровий режим НЕ відновлюється після ресету свідомо. По-перше, у гру,
  // яку ніхто не почав, грати нема кому - після перезавантаження доречніше
  // показати годинник. По-друге, це запобіжник: якби гра колись падала на
  // старті, збережений прапорець дав би boot-loop, з якого пристрій не
  // вийшов би сам. Рекорд при цьому зберігається (CFG_DINO_HIGHSCORE).
  // _apply: на старті ми лише ЧИТАЄМО збережене значення, тому писати його
  // назад у NVS (як робив display_brightness()) не потрібно.
  display_brightness_apply(configStorage.getInt(CFG_DISPLAY_BRIGHTNESS, 100), isAutoBrightness);
  Logger::info("ConfigStorage load done");

  Logger::info("\t- %s = %s", CFG_SHOW_CLOCK, showClock ? "ON" : "OFF");
  Logger::info("\t- %s = %s", CFG_SYS_AUTOBRIGHTNESS, isAutoBrightness ? "true" : "false");
  Logger::info("\t- %s = %d", CFG_DISPLAY_BRIGHTNESS, display.brightness());
  Logger::info("");
}

void setupLightSensor() {
#if BOARD_HAS_LIGHT_SENSOR
  lightSensor.begin();
  scheduler.addCronTask(0, []() { lightSensor.update(); });

  lightSensor.addListener([]() {
    Logger::info("lightSensor.value() = %4d (%3d%%)", lightSensor.read(), lightSensor.value());
    if (isAutoBrightness) {
      // _apply, а не display_brightness(): без запису в NVS на кожну зміну
      // показань сенсора (див. коментар біля display_brightness()).
      display_brightness_apply(lightSensor.value(), isAutoBrightness);
    }
  });

  /* scheduler.addCronTask(0, []() {
    display.setTextSize(1);
    display.setTextColor(TFT_DARKGREY);
    display.setCursor(10, display.height() - 1 * (5 + display.fontHeight()));
    display.printf("LightSensor: %4d (%3d%%)", lightSensor.read(), lightSensor.value());
  }); */

#endif
}

void setupDisplay() {
#if defined(BOARD_ST7789)
  pinMode(TFT_BL, OUTPUT);  // st7789
#endif

  display.init();
  // display.autobrightness(true);
  Logger::info("Display setup done.");
}
