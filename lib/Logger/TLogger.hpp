#pragma once

// Вибір бекенду логера. За замовчуванням - автовизначення платформи
// (ESP8266/ESP32 - стандартні platform defines).
//
// Можна форсувати конкретну реалізацію незалежно від платформи через
// build_flags, напр.:
//   build_flags = -D USE_SERIAL_LOGGER
// або
//   build_flags = -D USE_ESP_LOGGER
#if defined(USE_SERIAL_LOGGER)
#include "SerialLogger.hpp"
using TLogger = SerialLogger;
#elif defined(USE_ESP_LOGGER)
#include "EspLogger.hpp"
using TLogger = EspLogger;
#elif defined(ESP8266)
#include "SerialLogger.hpp"
using TLogger = SerialLogger;
#else
#include "EspLogger.hpp"
using TLogger = EspLogger;
#endif
