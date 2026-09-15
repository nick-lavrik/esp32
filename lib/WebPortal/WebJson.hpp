#pragma once

// Мінімальні хелпери для складання JSON вручну.
//
// Чому не ArduinoJson, який і так є в lib_deps. Документ там матеріалізується
// в heap цілком, а відповіді порталу - це переважно короткі об'єкти й списки,
// які дешевше зібрати одразу в String, що й так потрібен для
// AsyncWebServerRequest::send(). Для розбору ТІЛА запиту - навпаки, беремо
// ArduinoJson (див. WebPortal.cpp): писати власний парсер сенсу немає.
//
// Екранування - обов'язкове: у SSID цілком законно трапляються лапки й
// зворотні слеші, а рядок логу може містити будь-що, включно з керівними
// символами. Неекранований вивід ламав би весь JSON, а не одне поле.

#include <Arduino.h>

namespace webjson {

// Екранує рядок за RFC 8259 і додає лапки навколо.
inline String quote(const char* value) {
  String out;
  if (value == nullptr) return String("\"\"");

  out.reserve(strlen(value) + 2);
  out += '"';
  for (const char* p = value; *p != '\0'; ++p) {
    const unsigned char c = static_cast<unsigned char>(*p);
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (c < 0x20) {
          char esc[7];
          snprintf(esc, sizeof(esc), "\\u%04x", c);
          out += esc;
        } else {
          // Байти >= 0x80 проходять як є: логер уже гарантує коректний UTF-8
          // (обрізання рядків у SerialLogger UTF-8-безпечне).
          out += static_cast<char>(c);
        }
    }
  }
  out += '"';
  return out;
}

inline String quote(const String& value) { return quote(value.c_str()); }
inline String quote(const std::string& value) { return quote(value.c_str()); }

inline String boolean(bool value) { return value ? String("true") : String("false"); }

// {"error":"..."} - єдиний формат помилки для всіх роутів порталу.
inline String error(const char* message) { return String("{\"error\":") + quote(message) + "}"; }

// {"ok":<bool>,"message":"..."} - єдиний формат результату задачі WebJobQueue.
// Саме його розбирає сторінка (`runAction()` в assets/www/index.html), тому
// формат спільний для всіх модулів, а не власний у кожного.
inline String ok(const char* message) {
  return String("{\"ok\":true,\"message\":") + quote(message) + "}";
}

inline String fail(const char* message) {
  return String("{\"ok\":false,\"message\":") + quote(message) + "}";
}

}  // namespace webjson
