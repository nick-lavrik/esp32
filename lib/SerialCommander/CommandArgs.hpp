#pragma once

// Розбір аргументів команд, спільний для всіх registerCommand().

#include <Arduino.h>

// yes/on/true/1 -> true, no/off/false/0 -> false; решта - false як результат
// функції, щоб команда сказала "use: ..." замість мовчки вимкнути фічу.
// Раніше кожна команда порівнювала з "on" сама, і 'watchdog status' вимикав
// watchdog із записом у NVS.
inline bool parseBool(const String& v, bool& out) {
  if (v.equalsIgnoreCase("yes") || v.equalsIgnoreCase("on") || v.equalsIgnoreCase("true") ||
      v == "1") {
    out = true;
    return true;
  }
  if (v.equalsIgnoreCase("no") || v.equalsIgnoreCase("off") || v.equalsIgnoreCase("false") ||
      v == "0") {
    out = false;
    return true;
  }
  return false;
}
