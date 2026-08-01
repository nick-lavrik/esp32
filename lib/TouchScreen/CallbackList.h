#pragma once

#include <vector>
#include <algorithm>
#include <utility> // std::move

// Динамічний список колбеків - без фіксованого ліміту (як MAX_CALLBACKS
// раніше). Побудований за тим самим принципом, що й TaskController:
// std::vector зберігає елементи, кожен елемент має свій унікальний handle
// (аналог TaskId), додавання - push_back(), видалення - за handle.
//
// Темплейт мусить залишатись у заголовку - компілятор інстанціює його
// окремо для кожного конкретного типу колбека (TouchCallback, SwipeCallback
// і т.д.) саме в тому файлі, де його використовують.
//
// CallbackT зазвичай std::function<...> (як TaskCallback у TaskController) -
// це дозволяє підписуватись і звичайними функціями, і лямбдами-замиканнями.
template <typename CallbackT>
class CallbackList {
public:
    using Handle = int;

    // Додає колбек у кінець списку (без обмеження кількості).
    // cb переміщується (std::move) - без зайвої копії замикання.
    // Повертає handle для подальшого remove().
    Handle add(CallbackT cb) {
        Handle handle = _nextHandle++;
        _entries.push_back(Entry{handle, std::move(cb)});
        return handle;
    }

    // Видаляє колбек за handle, який повернув add().
    // Повертає true, якщо колбек із таким handle був знайдений і видалений.
    bool remove(Handle handle) {
        auto it = std::find_if(_entries.begin(), _entries.end(),
                                [handle](const Entry &e) { return e.handle == handle; });
        if (it == _entries.end()) return false;
        _entries.erase(it);
        return true;
    }

    void clear() {
        _entries.clear();
    }

    size_t size() const { return _entries.size(); }

    template <typename... Args>
    void invoke(Args... args) const {
        for (const auto &entry : _entries) {
            if (entry.cb) entry.cb(args...);
        }
    }

private:
    struct Entry {
        Handle handle;
        CallbackT cb;
    };

    std::vector<Entry> _entries;
    Handle _nextHandle = 0;
};
