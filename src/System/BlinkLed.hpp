#pragma once

// Світлодіод стану (BLINK_LED_PIN): патерн за станом плати (AP / пошук WiFi /
// синхронізація часу / підключення MQTT / робота), команда 'blink'.
// Режим (on|off|auto) - у NVS (CFG_BLINK_LED). Без BLINK_LED_PIN - нічого.
//
//   setupBlinkLED(commandHandler);  // з setup()

#include <SerialCommander.hpp>

void setupBlinkLED(SerialCommander& commander);
