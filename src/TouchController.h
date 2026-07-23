#pragma once
#include "TouchEvents.h"

// Єдина точка входу для роботи з тачскріном - однакова для обох плат.
//
// Клас лише ОГОЛОШЕНО тут (src/TouchController.h, компілюється завжди).
// РЕАЛІЗАЦІЯ (TouchController.cpp) - різна для кожної плати і підключається
// через build_src_filter у platformio.ini:
//   env:esp32-st7789   -> src-st7789/TouchController.cpp   (XPT2046, SPI)
//   env:esp32-4848s040 -> src-4848s040/TouchController.cpp (GT911, I2C)
//
// Завдяки цьому main.cpp (спільний для обох середовищ) працює з тачскріном
// однаково, не знаючи, яка апаратна бібліотека стоїть під капотом.
class TouchController {
public:
    // Ініціалізація апаратного тачскріна + TouchPointMapper.
    // Викликати один раз з setup().
    static void setup(TouchEvents* touch);

    // Опитування апаратного тачскріна і прокидання події в TouchEvents.
    // Викликати щоцикл з loop().
    static void update();

    static void setInstance(TouchEvents* instance);
    // Доступ до TouchEvents для підписки на онTouch/onSwipe/onHold і т.д.
    // Підписку можна робити в спільному main.cpp - вона однакова для обох плат.
    static TouchEvents &events();

private:
    // Статична змінна-вказівник для збереження екземпляру
    static TouchEvents* _events; 
};
