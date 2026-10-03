#pragma once

// Стан пристрою з консолі: 'status sys|cfg|sd|sd+|flash|flash+|littlefs',
// 'heap' (обсяг і фрагментація), 'heap-watch' (cron 'heap' раз на 2 хв,
// перемикач у NVS - CFG_HEAP_WATCH).

#include <SerialCommander.hpp>

void dumpSystemInfo();
void dumpStatus(const String& section);
void registerSystemStatusCommands(SerialCommander& commander);
