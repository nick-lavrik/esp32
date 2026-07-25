#include "Event.hpp"

void Event::stopPropagation() {
    _propagationStopped = true;
}

bool Event::isPropagationStopped() const {
    return _propagationStopped;
}
