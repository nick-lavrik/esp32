#pragma once

#include <functional>

#include "TouchPoint.h"

// Тип колбека для одиночного дотику (onTouch) та подвійного кліку (onDblClick).
// std::function, як і TaskCallback, дозволяє передавати як звичайні функції,
// так і лямбди із захопленням (closures).
// УВАГА щодо memory leaks: якщо лямбда захоплює вказівник на об'єкт,
// створений через `new`, звільняти його — відповідальність користувача.
using TouchCallback = std::function<void(TouchPoint point)>;
