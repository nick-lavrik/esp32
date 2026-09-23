#pragma once

// Один запис реєстру JSON-команд MQTT SAPI-каналу (docs/mqtt-web-handoff.md,
// фаза 1, розділ "Реєстр JSON-команд (JsonApiEntry)"). Пласка структура зі
// звичайними С-функціями (не std::function/лямбда із захопленням) - жодної
// можливості випадкової heap-алокації через замикання.

#include <ArduinoJson.h>
#include <WString.h>

#include <cstddef>
#include <cstdint>

struct JsonApiEntry {
  const char* name;

  // JSON-аргументи запиту -> типізована Args-структура конкретної команди,
  // байт-у-байт у rawOut (memcpy), з валідацією одразу. Команди без
  // аргументів (уся пре-альфа) нічого не читають і завжди повертають true.
  // false - аргументи невалідні: клієнт отримує "bad args", команда в чергу
  // не потрапляє.
  bool (*resolve)(JsonVariantConst args, uint8_t* rawOut, size_t rawCapacity);

  // raw - ті самі байти, що поклав resolve(). Повертає готовий JSON-об'єкт
  // (без обгортки "id"/"ok" - її додає CommandQueue).
  String (*execute)(const uint8_t* raw);
};
