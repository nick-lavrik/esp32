#pragma once
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
static inline long random(long hi) { return hi > 0 ? (rand() % hi) : 0; }
static inline uint32_t millis() { return 0; }
