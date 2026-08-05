#pragma once

#include <WString.h>  // Arduino String (arduino-esp32 core)

#include <cstdint>
#include <cstdio>

/**
 * @brief Хелпер для конвертації розміру в байтах у людяний (human-readable) рядок.
 *
 * Клас не має стану, всі методи статичні. Не потребує .cpp файлу.
 * Потребує Arduino framework (arduino-esp32 core) через клас String.
 *
 * Приклади:
 *   SizeFormatter::format(1536);              // "1.50 KiB"
 *   SizeFormatter::format(1000, 1, false);     // "1.0 KB"
 *   SizeFormatter::format(0);                  // "0 B"
 *
 * Примітка: String робить одну динамічну алокацію (не з ISR!). Для ISR
 * або суворих no-heap ділянок використовуйте formatTo() з char-буфером.
 */
class SizeFormatter {
public:
  // Максимальна довжина результату у formatTo(), включно з '\0'.
  // "1023.99 GiB" + запас -> достатньо 24 байти.
  static constexpr size_t kMaxBufferSize = 24;

  /**
   * @brief Конвертує розмір у байтах у рядок Arduino String.
   *
   * @param bytes         Розмір у байтах.
   * @param precision     Кількість знаків після коми (0..6). За замовчуванням 2.
   * @param useBinaryUnits true  -> основа 1024 (B, KiB, MiB, GiB, TiB, PiB)
   *                       false -> основа 1000 (B, KB, MB, GB, TB, PB)
   * @return String       Готовий рядок, напр. "12.34 MiB".
   *                      Не викликати з ISR (внутрішня алокація пам'яті).
   */
  static String format(uint64_t bytes, uint8_t precision = 2, bool useBinaryUnits = true) {
    char buffer[kMaxBufferSize];
    formatTo(buffer, sizeof(buffer), bytes, precision, useBinaryUnits);
    return String(buffer);  // одна алокація, без ланцюжка конкатенацій
  }

  /**
   * @brief Конвертує розмір у байтах у рядок без динамічної алокації (без купи).
   *        Рекомендовано для використання в переривань/задачах реального часу на ESP32.
   *
   * @param outBuffer     Буфер призначення (не nullptr).
   * @param outBufferSize Розмір буфера в байтах (рекомендовано >= kMaxBufferSize).
   * @param bytes         Розмір у байтах.
   * @param precision     Кількість знаків після коми (0..6).
   * @param useBinaryUnits true -> основа 1024, false -> основа 1000.
   * @return size_t       Кількість записаних символів (без '\0'), або 0 при помилці.
   */
  static size_t formatTo(char* outBuffer, size_t outBufferSize, uint64_t bytes,
                         uint8_t precision = 2, bool useBinaryUnits = true) {
    if (outBuffer == nullptr || outBufferSize == 0) {
      return 0;
    }

    if (precision > 6) {
      precision = 6;
    }

    static constexpr const char* kBinaryUnits[] = {"B", "Kb", "Mb", "Gb", "Tb", "Pb", "Eb"};
    static constexpr const char* kDecimalUnits[] = {"B", "KB", "MB", "GB", "TB", "PB", "EB"};
    static constexpr int kUnitCount = 7;

    const double base = useBinaryUnits ? 1024.0 : 1000.0;
    const char* const* units = useBinaryUnits ? kBinaryUnits : kDecimalUnits;

    // Байти виводимо без дробової частини.
    if (bytes < static_cast<uint64_t>(base)) {
      int written = std::snprintf(outBuffer, outBufferSize, "%llu %s",
                                  static_cast<unsigned long long>(bytes), units[0]);
      return clampWritten(written, outBufferSize);
    }

    double value = static_cast<double>(bytes);
    int unitIndex = 0;
    while (value >= base && unitIndex < kUnitCount - 1) {
      value /= base;
      ++unitIndex;
    }

    int written =
        std::snprintf(outBuffer, outBufferSize, "%.*f %s", precision, value, units[unitIndex]);
    return clampWritten(written, outBufferSize);
  }

private:
  SizeFormatter() = delete;  // тільки статичні методи, інстанціювання не потрібне

  static size_t clampWritten(int snprintfResult, size_t outBufferSize) {
    if (snprintfResult < 0) {
      return 0;
    }
    size_t written = static_cast<size_t>(snprintfResult);
    return (written < outBufferSize) ? written : (outBufferSize - 1);
  }
};
