#pragma once

#include <utility> // std::move

// Шаблонний контейнер колбеків фіксованого розміру (без heap, без std::vector
// для самого списку - лише слоти під std::function).
// Темплейти мусять залишатись у заголовку - компілятор інстанціює їх окремо
// для кожного конкретного типу колбека (TouchCallback, SwipeCallback і т.д.)
// саме в тому файлі, де їх використовують.
//
// CallbackT зазвичай std::function<...> (як TaskCallback у TaskScheduler) -
// це дозволяє підписуватись і звичайними функціями, і лямбдами-замиканнями.
//
// Дозволяє декілька підписників на один івент + додавання/видалення в рантаймі.
template <typename CallbackT, int MaxSlots>
class CallbackList {
public:
    // Повертає handle (>=0) для подальшого remove(), або -1, якщо немає вільних слотів.
    // cb переміщується (std::move) у слот - важливо для std::function із
    // захопленими об'єктами, щоб не робити зайву копію замикання.
    int add(CallbackT cb) {
        for (int i = 0; i < MaxSlots; i++) {
            if (!_used[i]) {
                _slots[i] = std::move(cb);
                _used[i] = true;
                return i;
            }
        }
        return -1;
    }

    bool remove(int handle) {
        if (handle < 0 || handle >= MaxSlots || !_used[handle]) return false;
        _used[handle] = false;
        _slots[handle] = nullptr;
        return true;
    }

    void clear() {
        for (int i = 0; i < MaxSlots; i++) { _used[i] = false; _slots[i] = nullptr; }
    }

    template <typename... Args>
    void invoke(Args... args) const {
        for (int i = 0; i < MaxSlots; i++) {
            if (_used[i] && _slots[i]) _slots[i](args...);
        }
    }

private:
    CallbackT _slots[MaxSlots] = {};
    bool _used[MaxSlots] = {};
};
