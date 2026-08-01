// ping.h
#pragma once

#if !__has_include(<ESP8266Ping.h>)

void doPing() { ; }
char* dumpPingStatsStr() { return nullptr; }

#else

#include <ESP8266Ping.h> // вбудований в ESP8266 Arduino core, окремий lib_dep не потрібен

// --- Пінг ---
const char* pingHost = "8.8.8.8";

#define PING_INTERVAL_MS 5000

int currentPing = -1; // -1 = timeout/помилка
int minPing = 9999, maxPing = 0;
long pingSum = 0;
int pingCount = 0;

void doPing() {
    static uint32_t lastUpdateMs = 0;
    uint32_t now = millis();

    if (now - lastUpdateMs < PING_INTERVAL_MS) return;
    lastUpdateMs = now;

    bool success = Ping.ping(pingHost, 1);
    if (success) {
        currentPing = static_cast<int>(Ping.averageTime());
        if (currentPing < minPing) minPing = currentPing;
        if (currentPing > maxPing) maxPing = currentPing;
        pingSum += currentPing;
        pingCount++;
    } else {
        currentPing = -1; // timeout
    }
}

char pingDumpStr[45];
char* dumpPingStatsStr() {
    int avgPing = pingCount > 0 ? (pingSum / pingCount) : 0;

    if (currentPing < 0) {
        sprintf(pingDumpStr, "PING: FAIL  %3d/%3d/%3d", minPing, avgPing, maxPing);
    } else {
        sprintf(pingDumpStr, "PING:%3dms %3d/%3d/%3d", currentPing, minPing, avgPing, maxPing);
    }

    return pingDumpStr;
}
#endif
