#pragma once

// Налаштування екрана, спільні для всіх режимів: годинник, яскравість
// (ручна й авто від датчика освітленості), поворот на 180°.
//
//   display_brightness(50, false);   // зберігає в NVS - для явних дій користувача
//   display_brightness_apply(v, true); // без NVS - для слухача датчика
//   show_clock(!showClock);
//
// Команди: flip, clock, brightness (registerScreenControlCommands()).

#include <stdint.h>

#include <SerialCommander.hpp>

// Годинник на основному екрані (CFG_SHOW_CLOCK).
extern bool showClock;
// Яскравість стежить за датчиком освітленості (CFG_SYS_AUTOBRIGHTNESS).
extern bool isAutoBrightness;

void display_brightness_apply(uint8_t percent, bool _auto);
void display_brightness(uint8_t percent, bool _auto);
void display_flip();
void show_clock(bool show);

// Прочитати з NVS годинник/яскравість/автояскравість - наприкінці setup().
// Ігровий режим свідомо НЕ відновлюється (див. коментар у ScreenControl.cpp).
void loadScreenSettings();
// Датчик освітленості (BOARD_HAS_LIGHT_SENSOR): опитування й автояскравість.
void setupLightSensor();

void registerScreenControlCommands(SerialCommander& commander);
