#pragma once

// Перелік типів даних
enum DataType {
  TYPE_EMPTY,
  TYPE_PRINTABLE_SHORT, // Рядок з 2-4 UTF-8 символів
  TYPE_JSON,
  TYPE_STRING,          // Звичайний текст
  TYPE_INT8_UINT8,      // 1 байт
  TYPE_INT16_UINT16,    // 2 байти
  TYPE_INT32_UINT32,    // 4 байти (ціле число)
  TYPE_FLOAT_DOUBLE,    // 4 або 8 байт (число з комою)
  TYPE_BINARY           // Потік байтів
};

void testGuessDataType();