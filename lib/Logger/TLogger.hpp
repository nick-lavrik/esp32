#pragma once

// Бекенд логера. Історично тут був вибір між SerialLogger і EspLogger через
// build_flags (-D USE_SERIAL_LOGGER / -D USE_ESP_LOGGER). Вибору більше немає:
// EspLogger видалений. Він писав напряму в esp_log_writev(), тобто повз
// журнал - без нього відповіді на MQTT-команди приходили б
// порожні, а дзеркало консолі показувало б порожній топік. Через це жоден env
// його не вмикав, і вмикати не планується.
//
// Аліас лишається, бо на нього спирається весь проєкт (`const TLogger
// _logger{"tag"}`) і бо він - точка, де бекенд можна буде підмінити ще раз.

#include "SerialLogger.hpp"

using TLogger = SerialLogger;
