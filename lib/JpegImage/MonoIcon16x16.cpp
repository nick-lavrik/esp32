#include "MonoIcon16x16.hpp"

// clang-format off
const uint8_t MonoIcon16x16::kDataWifi[32] PROGMEM = {
  0b00000000, 0b00000000, // 0x00, 0x00,
  0b00000000, 0b00000000, // 0x00, 0x00,
  0b00000000, 0b00000000, // 0x00, 0x00,
  0b01111111, 0b11111110, // 0x7F, 0xFE,
  0b10000000, 0b00000001, // 0x80, 0x01,
  0b10000000, 0b00000001, // 0x80, 0x01,
  0b00011111, 0b11111000, // 0x1F, 0xF8,
  0b00100000, 0b00000100, // 0x20, 0x04,
  0b00100000, 0b00000100, // 0x20, 0x04,
  0b00000111, 0b11100000, // 0x07, 0xE0,
  0b00001000, 0b00010000, // 0x08, 0x10,
  0b00001000, 0b00010000, // 0x08, 0x10,
  0b00000001, 0b10000000, // 0x01, 0x80,
  0b00000001, 0b10000000, // 0x01, 0x80,
  0b00000000, 0b00000000, // 0x00, 0x00,
  0b00000000, 0b00000000, // 0x00, 0x00,
};

const uint8_t MonoIcon16x16::kDataEmpty[32] PROGMEM = {
  0b00000000, 0b00000000, // 0x00, 0x00,
  0b00000000, 0b00000000, // 0x00, 0x00,
  0b00000000, 0b00000000, // 0x00, 0x00,
  0b00000000, 0b00000000, // 0x7F, 0xFE,
  0b00000000, 0b00000000, // 0x80, 0x01,
  0b00000000, 0b00000000, // 0x80, 0x01,
  0b00000000, 0b00000000, // 0x1F, 0xF8,
  0b00000000, 0b00000000, // 0x20, 0x04,
  0b00000000, 0b00000000, // 0x20, 0x04,
  0b00000000, 0b00000000, // 0x07, 0xE0,
  0b00000000, 0b00000000, // 0x08, 0x10,
  0b00000000, 0b00000000, // 0x08, 0x10,
  0b00000000, 0b00000000, // 0x01, 0x80,
  0b00000000, 0b00000000, // 0x01, 0x80,
  0b00000000, 0b00000000, // 0x00, 0x00,
  0b00000000, 0b00000000, // 0x00, 0x00,
};
// clang-format on

const MonoBitmap &MonoIcon16x16::wifi() {
  static const MonoBitmap instance(kDataWifi, kWidth, kHeight);
  return instance;
}

const MonoBitmap &MonoIcon16x16::empty() {
  static const MonoBitmap instance(kDataEmpty, kWidth, kHeight);
  return instance;
}
