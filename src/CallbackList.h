#pragma once

// Шаблонний контейнер колбеків фіксованого розміру (без heap, без std::vector).
// Темплейти мусять залишатись у заголовку - компілятор інстанціює їх окремо
// для кожного конкретного типу колбека (TouchCallback, SwipeCallback і т.д.)
// саме в тому файлі, де їх використовують.
//
// Дозволяє декілька підписників на один івент + додавання/видалення в рантаймі.
template <typename CallbackT, int MaxSlots>
class CallbackList {
public:
    // Повертає handle (>=0) для подальшого remove(), або -1, якщо немає вільних слотів
    int add(CallbackT cb) {
        for (int i = 0; i < MaxSlots; i++) {
            if (!_used[i]) {
                _slots[i] = cb;
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
