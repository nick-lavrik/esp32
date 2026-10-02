#pragma once

// Фонове зображення основного екрана: завантаження на старті й команди
// обробки (blur, tint, ..., background, bg-dump).
//
//   setupBackgroundImage();                     // з setup(), після setupDisplay()
//   registerBackgroundCommands(commandHandler); // з setupSerialCommander()
//
// Три взаємовиключні варіанти фону (src-<env>/environment.h): вшитий у
// прошивку / LITTLEFS_BACKGROUND_IMAGE (декодується в RAM) /
// BACKGROUND_PROGMEM_HEADER (запечений у Flash). Команди є лише з
// LITTLEFS_BACKGROUND_IMAGE: решта варіантів read-only. Матриця по платах -
// docs/architecture.md, «Матриця фіч по платах». Малювання фону -
// src/BackgroundImages.hpp.

#include <SerialCommander.hpp>

void setupBackgroundImage();

// Без LITTLEFS_BACKGROUND_IMAGE не реєструє нічого.
void registerBackgroundCommands(SerialCommander& commander);
