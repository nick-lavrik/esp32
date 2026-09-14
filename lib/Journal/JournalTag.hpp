#pragma once

// Ієрархічний матчинг тегів через крапку: "mqtt" ловить "mqtt", "mqtt.send" і
// "mqtt.send.heartbeat", але НЕ "mqttx".
//
// Одна функція обслуговує і підписку приймачів, і резолвинг рівня за тегом -
// раніше та сама логіка обходу ієрархії жила окремо в LogLevelManager.
//
// Порожній патерн - "усе". Це не окремий випадок у коді, а природний наслідок:
// префікс нульової довжини збігається з будь-чим.

#include <cstddef>

inline bool journalTagMatches(const char* pattern, const char* tag) {
  if (pattern == nullptr || pattern[0] == '\0') return true;
  if (tag == nullptr) return false;

  size_t i = 0;
  while (pattern[i] != '\0') {
    if (tag[i] != pattern[i]) return false;
    ++i;
  }

  // Збіглись до кінця патерна. Це справжній збіг, лише якщо тег на цьому й
  // закінчився ("mqtt" == "mqtt") або далі йде роздільник рівня ("mqtt.send").
  // Без цієї перевірки "mqtt" ловив би ще й "mqttx".
  return tag[i] == '\0' || tag[i] == '.';
}
