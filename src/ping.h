#pragma once

#if defined(BOARD_ESP8266)
#define HAS_PING_LIB __has_include(<ESP8266Ping.h>)
#else
#define HAS_PING_LIB __has_include(<ESPping.h>)
#endif

#if !HAS_PING_LIB

void doPing() { ; }
char* dumpPingStatsStr() { return nullptr; }

#else

#if defined(BOARD_ESP8266)
#include <ESP8266Ping.h>  // вбудований в ESP8266 Arduino core, окремий lib_dep не потрібен
#else
#include <ESPping.h>
#endif

// --- Пінг ---
const char* pingHost = "8.8.8.8";

#define PING_INTERVAL_MS 5000
#define GRAPH_POINTS 100

int pingHistory[GRAPH_POINTS];
int pingHistoryIndex = 0;
bool pingHistoryFull = false;
int currentPing = -1;  // -1 = timeout/помилка
int minPing = 9999, maxPing = 0;
long pingSum = 0;
int pingCount = 0;

void doPing() {
  static uint32_t lastUpdateMs = 0;
  static size_t currentIndex = 0;
  uint32_t now = millis();

  if (now - lastUpdateMs < PING_INTERVAL_MS) return;

  lastUpdateMs = now;

  bool success = Ping.ping(pingHost, 1);
  if (success) {
    currentPing = Ping.averageTime();
    if (currentPing < minPing) minPing = currentPing;
    if (currentPing > maxPing) maxPing = currentPing;
    pingSum += currentPing;
    pingCount++;
  } else {
    currentPing = -1;  // timeout
  }

  pingHistory[pingHistoryIndex] = currentPing;
  pingHistoryIndex = (pingHistoryIndex + 1) % GRAPH_POINTS;
  if (pingHistoryIndex == 0) pingHistoryFull = true;
}

char pingDumpStr[45];
char* dumpPingStatsStr() {
  int avgPing = pingCount > 0 ? (pingSum / pingCount) : 0;

  if (currentPing < 0) {
    sprintf(pingDumpStr, "PING: FAIL   %3d / %3d / %3d ms\n", minPing, avgPing, maxPing);
  } else {
    sprintf(pingDumpStr, "PING: %3dms  %3d / %3d / %3d ms\n", currentPing, minPing, avgPing,
            maxPing);
  }

  return pingDumpStr;
}
#endif
