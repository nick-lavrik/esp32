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
#include <ESP8266WiFi.h>
#include <ESP8266Ping.h>  // вбудований в ESP8266 Arduino core, окремий lib_dep не потрібен
#else
#include <WiFi.h>
#include <ESPping.h>
#endif

// --- Пінг ---
const char* pingHost = "8.8.8.8";

#define PING_INTERVAL_MS 5000

// УВАГА: Ping.ping() - БЛОКУЮЧИЙ (до ~1 с на спробу), а doPing() викликається
// першим рядком loop(). Тобто раз на PING_INTERVAL_MS весь цикл (дисплей,
// mqtt.loop(), обробка команд) стоїть. Прибрати можна лише переїздом на
// асинхронний пінг або окремий таск - тут свідомо лишено як є.
int currentPing = -1;  // -1 = timeout/помилка
int minPing = 0, maxPing = 0;
long pingSum = 0;
int pingCount = 0;

void doPing() {
  static uint32_t lastUpdateMs = 0;
  uint32_t now = millis();

  if (now - lastUpdateMs < PING_INTERVAL_MS) return;

  lastUpdateMs = now;

  // Без WiFi пінгувати нема куди, а Ping.ping() все одно чесно відпрацював би
  // весь свій таймаут - тобто найгірший випадок блокування loop() траплявся б
  // саме тоді, коли пристрій і без того в поганому стані.
  if (!WiFi.isConnected()) {
    currentPing = -1;
    return;
  }

  bool success = Ping.ping(pingHost, 1);
  if (success) {
    currentPing = Ping.averageTime();
    // minPing стартує з 0, тому перший успішний замір задає обидві межі -
    // раніше стартове значення 9999 показувалось як "min", поки не траплявся
    // пінг гірший за 9999 мс (тобто практично назавжди).
    if (pingCount == 0 || currentPing < minPing) minPing = currentPing;
    if (currentPing > maxPing) maxPing = currentPing;
    pingSum += currentPing;
    pingCount++;
  } else {
    currentPing = -1;  // timeout
  }
}

char pingDumpStr[48];
char* dumpPingStatsStr() {
  int avgPing = pingCount > 0 ? (int)(pingSum / pingCount) : 0;

  // snprintf, не sprintf: буфер фіксований, а значення пінгу приходять від
  // мережі й теоретично можуть бути чотири- і більше-значними.
  if (currentPing < 0) {
    snprintf(pingDumpStr, sizeof(pingDumpStr), "PING: FAIL   %3d / %3d / %3d ms\n", minPing, avgPing,
             maxPing);
  } else {
    snprintf(pingDumpStr, sizeof(pingDumpStr), "PING: %3dms  %3d / %3d / %3d ms\n", currentPing,
             minPing, avgPing, maxPing);
  }

  return pingDumpStr;
}
#endif
