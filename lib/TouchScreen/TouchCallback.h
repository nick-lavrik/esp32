#pragma once

#include <functional>

#include "TouchPoint.h"

// Тип колбека для точкових подій тачскріна: onTouch (натискання),
// onRelease (відпускання), onClick (тап) і onDblClick (подвійний тап).
// std::function, як і TaskCallback, дозволяє передавати як звичайні функції,
// так і лямбди із захопленням (closures).
// УВАГА щодо memory leaks: якщо лямбда захоплює вказівник на об'єкт,
// створений через `new`, звільняти його — відповідальність користувача.
using TouchCallback = std::function<void(TouchPoint point)>;
