#pragma once

#include <functional>
#include "TouchPoint.h"

// Тип колбека для утримання дотику (onHold).
// std::function, як і TaskCallback, дозволяє передавати як звичайні функції,
// так і лямбди із захопленням (closures).
using HoldCallback = std::function<void(TouchPoint point, unsigned long holdDurationMs)>;
