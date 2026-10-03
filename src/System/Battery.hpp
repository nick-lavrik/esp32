#pragma once

// Напруга акумулятора через дільник BAT_ADC (BOARD_HAS_BATTERY_ADC,
// BATTERY_ADC_PIN / BATTERY_ADC_DIVIDER у src-<env>/environment.h), команда
// 'battery'. Без BOARD_HAS_BATTERY_ADC - нічого.
//
//   registerBatteryCommands(commandHandler);  // з setupSerialCommander()

#include <SerialCommander.hpp>

void registerBatteryCommands(SerialCommander& commander);
