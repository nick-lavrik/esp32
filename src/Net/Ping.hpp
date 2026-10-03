#pragma once

// Пінг 8.8.8.8 раз на 5 с і рядок статистики для основного екрана.
//
//   doPing();             // з loop(), крім екранів реального часу (блокує до ~1 с)
//   dumpPingStatsStr();   // "PING: ..." або nullptr, якщо HAS_PING=0

// HAS_PING - явний прапорець з src-<env>/environment.h (розділ 2), не
// виведений з __has_include(): той самий принцип, що й HAS_WEB_PORTAL/
// HAS_ECOFLOW_CLIENT там (CLAUDE.md) - компілятор і IDE-індексатор мають
// бачити ОДНЕ й те саме значення.
#ifndef HAS_PING
#error "HAS_PING is not defined - add #define HAS_PING 0/1 to this env's environment.h"
#endif

#if HAS_PING
void doPing();
char* dumpPingStatsStr();
#else
inline void doPing() {}
inline char* dumpPingStatsStr() { return nullptr; }
#endif
