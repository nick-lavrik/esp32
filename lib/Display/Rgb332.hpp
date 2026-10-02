#pragma once

#include <stdint.h>

// RGB332 (3-3-2, 1 байт/піксель) -> RGB565 бітовою реплікацією каналів.
// Окремий заголовок, а не метод Display: ту саму конвертацію потребують і
// драйвер (TftEspiDriver::pushImage8bpp() на бекендах без нативного 8bpp
// canvas), і test-gfx (палітра 256 кольорів) - друга копія була б рівно тим
// дублюванням, проти якого застерігає CLAUDE.md (WiFi_getAuthTypeName()/
// encryptionName()).
inline uint16_t rgb332to565(uint8_t c) {
  uint8_t r3 = (c >> 5) & 0x07;
  uint8_t g3 = (c >> 2) & 0x07;
  uint8_t b2 = c & 0x03;
  uint16_t r5 = (uint16_t)((r3 << 2) | (r3 >> 1));  // 0..7 -> 0..31
  uint16_t g6 = (uint16_t)(g3 * 9);                 // 0..7 -> 0..63 (7*9=63)
  uint16_t b5 = (uint16_t)(b2 * 10);                // 0..3 -> 0..30 (~5-біт, похибка ≤1)
  return (uint16_t)((r5 << 11) | (g6 << 5) | b5);
}
