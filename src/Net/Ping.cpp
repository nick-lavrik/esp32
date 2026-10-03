#include "Ping.hpp"

#include <Arduino.h>

// Наявність бібліотеки перевіряється окремо від HAS_PING (Ping.hpp) - якщо хтось
// виставить HAS_PING=1 без відповідного lib_dep (чи навпаки забуде
// прибрати HAS_PING=1 при вимкненні lib_dep), збірка провалюється явно
// (#error) замість мовчазного "ping ніколи не працює" в рантаймі.
#if defined(BOARD_ESP8266)
#if HAS_PING && !__has_include(<ESP8266Ping.h>)
#error \
    "HAS_PING=1, but ESP8266Ping.h is unavailable - add it to this env's lib_deps (not bundled with the current core, checked 2026-09-23)"
#endif
#else
#if HAS_PING && !__has_include(<ESPping.h>)
#error "HAS_PING=1, but dvarrel/ESPping is missing from this env's lib_deps"
#endif
#endif

#if HAS_PING

#if defined(BOARD_ESP8266)
#include <ESP8266Ping.h>  // НЕ вбудований у поточний core (перевірено 2026-09-23,
#include <ESP8266WiFi.h>
// "find / -iname ESP8266Ping.h" - нуль збігів) - потрібен
// окремий lib_dep, якщо колись HAS_PING=1 тут стане реальним
#else
#include <ESPping.h>
#include <WiFi.h>
#endif

namespace {

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

char pingDumpStr[48];

}  // namespace

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

char* dumpPingStatsStr() {
  int avgPing = pingCount > 0 ? (int)(pingSum / pingCount) : 0;

  // snprintf, не sprintf: буфер фіксований, а значення пінгу приходять від
  // мережі й теоретично можуть бути чотири- і більше-значними.
  if (currentPing < 0) {
    snprintf(pingDumpStr, sizeof(pingDumpStr), "PING: FAIL   %3d / %3d / %3d ms\n", minPing, avgPing, maxPing);
  } else {
    snprintf(pingDumpStr, sizeof(pingDumpStr), "PING: %3dms  %3d / %3d / %3d ms\n", currentPing, minPing, avgPing,
             maxPing);
  }

  return pingDumpStr;
}

#endif  // HAS_PING
