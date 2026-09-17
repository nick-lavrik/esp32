#pragma once

#include <Arduino.h>

#include "TLogger.hpp"

// withTrace() - огортає виклик колбека одним підсумковим рядком: тривалість
// виконання і heap до/після/дельта. Той самий "with"-паттерн, що й
// withMqttSuspended() (src/Ecoflow/EcoflowClient.hpp) - ім'я одразу каже, що
// це обгортка навколо чужого виклику, а не самостійна дія.
//
// Навіщо. Разові заміри важких кроків (setupWebPortal(), інші повільні
// setupXxx()) інакше означали б millis()/ESP.getFreeHeap() до і після в
// кожному місці виклику окремо - і саме так це й було зроблено вперше при
// вимірюванні вартості порталу. Тут той самий замір, але один раз.
//
// Власний тег "trace", а не логер модуля-виклика. Це окремий сигнал (timing +
// heap-дельта, за духом OpenTelemetry span), а не звичайний лог модуля: якби
// withTrace() писав під тегом виклика ("web", "ecoflow"...), довелось би
// вмикати рівень окремо для кожного тега, який колись обгорнуть. Один тег -
// один перемикач для всіх трас одразу: `journal level trace debug`.
//
// Рівень - Debug (DEFAULT_LOG_LEVEL=3 в platformio.ini), тобто видно з коробки,
// без додаткової команди. Verbose тут навмисно НЕ використано: цей рівень у
// проєкті зарезервований під дійсно шумні джерела (дотики, посимвольний
// розбір) - trace-рядок один на виклик, шуму не додає.
//
// _traceLog() винесено з самого withTrace() навмисно: withTrace<Fn> -
// шаблон, і кожна лямбда дає СВІЙ тип Fn, тобто свою інстанціацію функції.
// static TLogger усередині шаблонної функції дублювався б на кожне місце
// виклику (перевірено: 21 setupXxx() дали 21 окрему копію логера й тегу).
// Нешаблонна inline-функція - одна на весь проєкт, як і має бути.
//
// Формат рядка - фіксована ширина полів (охайні логи, docs/tech_debt.md §4):
// `grep "\[D\]\[trace  \]"` дає рівну табличку без жодної подальшої обробки,
// а awk/csv - вже опціонально. kMessageWidth=24 узятий під найдовший наразі
// виклик, "setupNetworkSupervisor" (22 символи) + запас.
inline void _traceLog(const char* message, uint32_t uptimeMs, uint32_t timerMs, uint32_t heapBefore,
                       uint32_t heapAfter) {
  static constexpr int kMessageWidth = 24;
  static TLogger _traceLogger{"trace"};
  _traceLogger.debug("%-*s uptime=%6lu ms  timer=%5lu ms  heap %6u -> %6u  delta=%7ld", kMessageWidth,
                      message, (unsigned long)uptimeMs, (unsigned long)timerMs, (unsigned)heapBefore,
                      (unsigned)heapAfter, (long)heapAfter - (long)heapBefore);
}

template <typename Fn>
inline void withTrace(const char* message, Fn&& callback) {
  const uint32_t uptimeMs = millis();
  const uint32_t heapBefore = ESP.getFreeHeap();
  callback();
  const uint32_t timerMs = millis() - uptimeMs;
  const uint32_t heapAfter = ESP.getFreeHeap();
  _traceLog(message, uptimeMs, timerMs, heapBefore, heapAfter);
}
