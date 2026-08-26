#include <Arduino.h> // Видаліть або замініть на <cstdio>, <cstring>, <cmath> для чистого C++
#include "DataType.h"

// Буфер, який ви зарезервували.
// static - навмисно: глобальний символ із зовнішнім зв'язуванням і настільки
// загальним іменем як "buf" рано чи пізно зіткнеться з таким самим у якійсь
// бібліотеці з lib_deps.
static char buf[128];


// === ДОПОМІЖНІ ФУНКЦІЇ ДЛЯ АНАЛІЗУ ===

// Перевірка на ASCII друковані символи
bool isPrintableChar(uint8_t c) {
  return (c >= 32 && c <= 126) || c == '\r' || c == '\n' || c == '\t';
}

// Перевірка на текст з урахуванням UTF-8 кирилиці
bool isPrintableUTF8(const uint8_t* payload, unsigned int length) {
  for (unsigned int i = 0; i < length; i++) {
    uint8_t c = payload[i];
    if (c == 0 && i == length - 1) continue; // Кінцевий нуль дозволено

    if (isPrintableChar(c)) continue;

    // Перевірка двобайтової UTF-8 кирилиці (0xD0 чи 0xD1)
    if ((c == 0xD0 || c == 0xD1 || c == 0xD2) && (i + 1 < length)) {
      uint8_t next = payload[i + 1];
      if (next >= 0x80 && next <= 0xBF) {
        i++; // Пропускаємо другий байт символу
        continue;
      }
    }
    return false; // Знайдено нетекстовий байт
  }
  return true;
}

// Рахуємо реальні символи (літери) в UTF-8
unsigned int countUTF8Characters(const uint8_t* payload, unsigned int length) {
  unsigned int count = 0;
  for (unsigned int i = 0; i < length; i++) {
    if (payload[i] == 0 && i == length - 1) break;
    // В UTF-8 байти продовження символу завжди починаються з бітів 10xxxxxx
    if ((payload[i] & 0xC0) != 0x80) {
      count++;
    }
  }
  return count;
}

// === ОСНОВНА ФУНКЦІЯ ВИЗНАЧЕННЯ ТИПУ ===

DataType guess_type(const uint8_t* payload, unsigned int length) {
  if (length == 0 || payload == nullptr) return TYPE_EMPTY;

  // 1. Перевіряємо, чи це текст (ASCII або UTF-8 Кирилиця)
  if (isPrintableUTF8(payload, length)) {
    
    // Шукаємо межі для перевірки JSON (ігноруючи пробіли)
    unsigned int first = 0;
    while (first < length && (payload[first] == ' ' || payload[first] == '\n' || payload[first] == '\r')) first++;
    
    unsigned int last = length - 1;
    while (last > first && (payload[last] == ' ' || payload[last] == '\n' || payload[last] == '\r' || payload[last] == 0)) last--;

    if (first < length && last >= first) {
      if ((payload[first] == '{' && payload[last] == '}') || (payload[first] == '[' && payload[last] == ']')) {
        return TYPE_JSON;
      }
    }

    // Перевірка умови "2 або 4 символи"
    unsigned int charCount = countUTF8Characters(payload, length);
    if (charCount == 2 || charCount == 4) {
      return TYPE_PRINTABLE_SHORT;
    }

    return TYPE_STRING;
  }

  // 2. Аналіз бінарних даних за довжиною пакета
  switch (length) {
    case 1: return TYPE_INT8_UINT8;
    case 2: return TYPE_INT16_UINT16;
    case 4: {
      float f;
      memcpy(&f, payload, 4);
      // Евристика для float (перевірка на валідність числа)
      if (!isnan(f) && isfinite(f) && f > -1000000.0 && f < 1000000.0 && f != 0.0) {
        return TYPE_FLOAT_DOUBLE;
      }
      return TYPE_INT32_UINT32;
    }
    case 8: {
      double d;
      memcpy(&d, payload, 8);
      if (!isnan(d) && isfinite(d) && d > -1000000.0 && d < 1000000.0 && d != 0.0) {
        return TYPE_FLOAT_DOUBLE;
      }
      return TYPE_BINARY;
    }
    default: return TYPE_BINARY;
  }
}

// === ФУНКЦІЯ ФОРМАТУВАННЯ В SNPRINTF ===

void format_payload_data(DataType type, const uint8_t* payload, unsigned int length, char* out_buf, unsigned int buf_size) {
  if (type == TYPE_EMPTY || payload == nullptr || length == 0) {
    snprintf(out_buf, buf_size, "[Empty]");
    return;
  }

  switch (type) {
    // TYPE_EMPTY відсіяно раннім return вище; перелічуємо явно, щоб -Wswitch
    // і далі ловив НОВІ значення DataType, які забули тут обробити.
    case TYPE_EMPTY:
      snprintf(out_buf, buf_size, "[Empty]");
      break;

    case TYPE_JSON:
    case TYPE_PRINTABLE_SHORT:
    case TYPE_STRING: {
      // Розраховуємо довжину рядка, щоб уникнути сміття, якщо немає нуль-термінатора
      unsigned int str_len = length;
      if (payload[length - 1] == 0) str_len--;
      
      // Форматуємо як рядок із обмеженням по довжині виводу (%.*s)
      snprintf(out_buf, buf_size, "TXT: \"%.*s\"", (int)str_len, (const char*)payload);
      break;
    }

    case TYPE_INT8_UINT8: {
      snprintf(out_buf, buf_size, "INT8: %d | UINT8: %u", (int8_t)payload[0], payload[0]);
      break;
    }

    case TYPE_INT16_UINT16: {
      int16_t v_signed;
      uint16_t v_unsigned;
      memcpy(&v_signed, payload, 2);
      memcpy(&v_unsigned, payload, 2);
      snprintf(out_buf, buf_size, "INT16: %d | UINT16: %u", v_signed, v_unsigned);
      break;
    }

    case TYPE_INT32_UINT32: {
      int32_t v_signed;
      uint32_t v_unsigned;
      memcpy(&v_signed, payload, 4);
      memcpy(&v_unsigned, payload, 4);
      // %ld та %lu використовуються для надійності на 8-бітних/32-бітних архітектурах
      snprintf(out_buf, buf_size, "INT32: %ld | UINT32: %lu", (long)v_signed, (unsigned long)v_unsigned);
      break;
    }

    case TYPE_FLOAT_DOUBLE: {
      if (length == 4) {
        float v_float;
        memcpy(&v_float, payload, 4);
        // Примітка: на деяких платах (наприклад, Arduino AVR) %f у snprintf вимкнено за замовчуванням.
        // Якщо замість числа виводить просто "?", використовуйте dtostrf() або платформи типу ESP32/ARM.
        snprintf(out_buf, buf_size, "FLOAT: %.4f", v_float);
      } else {
        double v_double;
        memcpy(&v_double, payload, 8);
        snprintf(out_buf, buf_size, "DOUBLE: %.6f", v_double);
      }
      break;
    }

    case TYPE_BINARY: {
      // Форматуємо перші кілька байтів у HEX, скільки поміститься у буфер
      unsigned int offset = snprintf(out_buf, buf_size, "BIN[%u]: ", length);
      for (unsigned int i = 0; i < length && offset < buf_size - 3; i++) {
        offset += snprintf(out_buf + offset, buf_size - offset, "%02X ", payload[i]);
      }
      break;
    }
  }
}

// === ПРИКЛАД ВИКОРИСТАННЯ В ОДНОМУ ВИКЛИКУ ===

void process_and_log(const uint8_t* payload, unsigned int length) {
  // 1. Вгадуємо тип даних
  DataType detectedType = guess_type(payload, length);

  // 2. Форматуємо текстовий опис у ваш зарезервований буфер buf (128 байт)
  format_payload_data(detectedType, payload, length, buf, sizeof(buf));

  // 3. Тепер у змінній buf лежить готовий рядок. Виводимо його куди завгодно:
  Serial.println(buf); 
}

// === СТАНДАРТНІ ФУНКЦІЇ ARDUINO ДЛЯ ТЕСТУ ===

void testGuessDataType() {

  Serial.println("--- TYPE HASHER TESTING ---");

  // Тест 1: Короткий рядок кириличних букв (2 символи "ОК")
  uint8_t test1[] = {0xD0, 0x9E, 0xD0, 0x9A}; // "ОК" в UTF-8
  process_and_log(test1, sizeof(test1));

  // Тест 2: JSON-текст
  uint8_t test2[] = "{\"v\":23}";
  process_and_log(test2, sizeof(test2) - 1); // -1 щоб не передавати нуль-термінатор

  // Тест 3: Бінарний float (число 1.0f)
  uint8_t test3[] = {0x00, 0x00, 0x80, 0x3F};
  process_and_log(test3, sizeof(test3));

  // Тест 4: Звичайний бінарний потік
  uint8_t test4[] = {0xDE, 0xAD, 0xBE, 0xEF, 0x01, 0x02};
  process_and_log(test4, sizeof(test4));
}

