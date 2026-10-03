#include "SystemControl.hpp"

#include <CommandArgs.hpp>
#include <Journal.hpp>
#include <Logger.hpp>
#include <SystemReset.hpp>
#include <TLogger.hpp>
#include <Watchdog.hpp>

#include "App/AppGlobals.hpp"

#if defined(ESP32) && __has_include(<soc/rtc_cntl_reg.h>)
#include <soc/rtc_cntl_reg.h>
// Регістр примусового download-boot для команди "bootloader".
//
// Перевіряти НАЯВНІСТЬ ЗАГОЛОВКА тут недостатньо: на класичному ESP32 (xtensa,
// ttgo-t1 / esp32-st7789) soc/rtc_cntl_reg.h є, але самого RTC_CNTL_OPTION1_REG
// у ньому немає - у того чипа download mode вмикається лише апаратно (GPIO0 на
// ресеті). Тому дивимось на сам макрос, інакше збірка падає з
// "'RTC_CNTL_OPTION1_REG' was not declared in this scope".
#if defined(RTC_CNTL_OPTION1_REG) && defined(RTC_CNTL_FORCE_DOWNLOAD_BOOT)
#define HAS_FORCE_DOWNLOAD_BOOT 1
#endif
#endif

void setupWatchdog() {
  // Останнім рядком - після setup(), а не на початку: сам setup() законно
  // довший за timeoutMs (TLS-хендшейки EcoFlow/MQTT, декодування фону), і
  // жоден з його кроків не годує watchdog.
  //
  // Опційний, за зразком ECOFLOW_AUTOCONNECT/ECOFLOW_SYNC_ON_BOOT: build-time
  // дефолт (WATCHDOG_ENABLED, src/App/ConfigKeys.hpp) + runtime-override у
  // ConfigStorage ('watchdog on|off', застосовується з наступного ребуту).
  // Вимикається свідомо, а не годується з довгих SD-команд (sdbench/sdcrc/
  // sdmap) - ці команди живуть у lib/SDRawReader, яка НЕ повинна знати про
  // Watchdog застосунку (lib/SystemReset/Watchdog.hpp).
  String watchdogStored = configStorage.getString(CFG_WATCHDOG, "");
  const bool watchdogEnabled = watchdogStored.length() > 0 ? (watchdogStored.toInt() != 0) : (WATCHDOG_ENABLED != 0);
  if (watchdogEnabled) {
    Watchdog::begin();
  } else {
    Logger::warn("watchdog disabled ('watchdog on' + reboot to re-enable)");
  }
}

void registerSystemControlCommands(SerialCommander& commander) {
  commander.registerCommand("reboot", "reboot device (soft reset)", [](const String& args) {
    dispatcher.dispatch(EVT_REBOOT);
#if HAS_MQTT_CLIENT
    // EVT_REBOOT вище призводить до mqtt.disconnect("reboot"), але в
    // PicoMQTT-гілці це лише кладе publish у чергу вихідних команд - без
    // очікування offline-LWT не встигав піти до брокера до ESP.restart().
    mqtt.flushOutgoing(500);
#endif
#if defined(BOARD_ESP8266)
    Logger::info("rebooting");
    Journal::instance().flushBlocking(300);
    delay(100);  // не Serial.flush(): на USB CDC він викидає TX, а не дочікує
    ESP.restart();
#else
        SystemReset::reboot();
#endif
  });

  // command: watchdog
  //
  // Той самий патерн, що ecoflow-auto/ecoflow-sync: build-time дефолт
  // (WATCHDOG_ENABLED) + runtime-override у ConfigStorage, що діє з
  // наступного ребуту - не живий перемикач посеред сесії. Вимикати перед
  // sdbench/sdcrc/sdmap (свідомо блокують loop() на десятки секунд) і
  // вмикати назад після.
  commander.registerCommand(
      "watchdog", "loop() hang watchdog, auto-reset on freeze: watchdog [on|off] (applies on next boot)",
      [](const String args) {
        static const TLogger _log{"wdog"};
        String value = args;
        value.trim();
        if (value.length() == 0) {
          String stored = configStorage.getString(CFG_WATCHDOG, "");
          _log.info("watchdog = %s%s",
                    stored.length() > 0 ? (stored.toInt() ? "on" : "off") : (WATCHDOG_ENABLED ? "on" : "off"),
                    stored.length() > 0 ? "" : " (build-time default)");
          return;
        }
        bool on = false;
        if (!parseBool(value, on)) {
          _log.warn("use: watchdog [on|off]");
          return;
        }
        configStorage.setString(CFG_WATCHDOG, on ? "1" : "0");
        _log.info("watchdog = %s (applies on next boot)", on ? "on" : "off");
      });

#if defined(HAS_FORCE_DOWNLOAD_BOOT)
  commander.registerCommand("bootloader", "reboot into ROM download mode (for flashing without BOOT/RESET buttons)",
                            [](const String& args) {
                              // НАВІЩО: на платах з native USB (S3 у режимі ARDUINO_USB_MODE=0)
                              // esptool не може сам перевести плату в завантажувач - послідовність
                              // DTR/RTS, якою він це робить через апаратний CDC, у TinyUSB не
                              // відтворюється, і прошивка падає з "No serial data received".
                              // Єдиною альтернативою лишалося тримати BOOT і тиснути RESET руками.
                              //
                              // Цей регістр - той самий шлях, яким користується сам ROM: прапорець
                              // примусового download-boot зберігається в RTC-домені, тому переживає
                              // перезапуск ядра.
                              Logger::warn("rebooting into bootloader (download mode)");
                              Journal::instance().flushBlocking(300);
                              delay(100);  // не Serial.flush(): на USB CDC він викидає TX, а не дочікує
                              REG_WRITE(RTC_CNTL_OPTION1_REG, RTC_CNTL_FORCE_DOWNLOAD_BOOT);
                              esp_restart();
                            });
#endif
}
