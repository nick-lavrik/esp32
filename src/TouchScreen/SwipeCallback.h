#pragma once

#include <functional>
#include "TouchPoint.h"

// Тип колбека для свайпів (onSwipeLeft/Right/Up/Down, onSwipeFromXxx).
// std::function, як і TaskCallback, дозволяє передавати як звичайні функції,
// так і лямбди із захопленням (closures).
using SwipeCallback = std::function<void(TouchPoint start, TouchPoint end)>;
