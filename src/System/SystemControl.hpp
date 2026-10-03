#pragma once

// Керування пристроєм: 'reboot', 'watchdog [on|off]' (діє з наступного
// ребуту), 'bootloader' (лише чипи з примусовим download-boot у RTC).
//
//   setupWatchdog();  // ОСТАННІМ рядком setup()

#include <SerialCommander.hpp>

void setupWatchdog();
void registerSystemControlCommands(SerialCommander& commander);
