# Довідник по `<type_traits>` (C++17 / gnu++17)

## 1. Основні категорії типів (Primary type categories)
Кожен тип належить **рівно до однієї** з цих категорій.

```cpp
#include <type_traits>

void function() {}
union MyUnion { int x; float y; };
class MyClass {};
enum MyEnum { A, B };

// Перевірки (усі вирази повертають true)
static_assert(std::is_void_v<void>);
static_assert(std::is_null_pointer_v<std::nullptr_t>);
static_assert(std::is_integral_v<int> && std::is_integral_v<bool>);
static_assert(std::is_floating_point_v<double> && std::is_floating_point_v<float>);
static_assert(std::is_array_v<int> && std::is_array_v<int[]>);
static_assert(std::is_pointer_v<int*> && !std::is_pointer_v<int&>);
static_assert(std::is_lvalue_reference_v<int&>);
static_assert(std::is_rvalue_reference_v<int&&>);
static_assert(std::is_member_object_pointer_v<int MyClass::*>);
static_assert(std::is_member_function_pointer_v<void (MyClass::*)()>);
static_assert(std::is_enum_v<MyEnum>);
static_assert(std::is_union_v<MyUnion>);
static_assert(std::is_class_v<MyClass>); // Для union поверне false
static_assert(std::is_function_v<decltype(function)>);
```

## 2. Складені категорії типів (Composite type categories)
Об'єднують кілька базових категорій для зручності.

```cpp
#include <type_traits>

static_assert(std::is_arithmetic_v<int> && std::is_arithmetic_v<float>); // integral + floating_point
static_assert(std::is_fundamental_v<void> && std::is_fundamental_v<int>); // void + arithmetic + nullptr_t
static_assert(std::is_object_v<int> && std::is_object_v<int>); // не посилання і не функції
static_assert(std::is_scalar_v<int*> && std::is_scalar_v<int> && std::is_scalar_v<std::nullptr_t>); 
static_assert(std::is_compound_v<int&> && std::is_compound_v<class T>); // все, що не fundamental
static_assert(std::is_reference_v<int&> && std::is_reference_v<int&&>); // lvalue + rvalue
```

## 3. Властивості типів (Type properties)
Визначають внутрішні характеристики типу чи кваліфікатори.

```cpp
#include <type_traits>

struct Trivial { int x; };
struct NonTrivial { NonTrivial() {} };

static_assert(std::is_const_v<const int>);
static_assert(std::is_volatile_v<volatile int>);
static_assert(std::is_trivial_v<Trivial> && !std::is_trivial_v<NonTrivial>);
static_assert(std::is_trivially_copyable_v<Trivial>); // можна копіювати через memcpy
static_assert(std::is_standard_layout_v<Trivial>);    // сумісність розташування з мовою C
static_assert(std::is_pod_v<Trivial>);                // Plain Old Data (deprecated у C++20)
static_assert(std::is_signed_v<int> && std::is_unsigned_v<unsigned int>);
```

## 4. Підтримка операцій (Supported operations)
Перевіряють наявність конструкторів, деструкторів чи операторів та їх безпеку (`nothrow`/`trivial`).

```cpp
#include <type_traits>

struct Agro { Agro(int) {} }; // немає дефолтного конструктора

static_assert(std::is_constructible_v<Agro, int>);
static_assert(std::is_default_constructible_v<int> && !std::is_default_constructible_v<Agro>);
static_assert(std::is_copy_constructible_v<int>);
static_assert(std::is_move_constructible_v<int>);
static_assert(std::is_assignable_v<int&, int>);
static_assert(std::is_copy_assignable_v<int>);
static_assert(std::is_move_assignable_v<int>);
static_assert(std::is_destructible_v<int>);
static_assert(std::is_trivially_destructible_v<int>);
static_assert(std::is_nothrow_destructible_v<int>);
static_assert(!std::has_virtual_destructor_v<int>);
```

## 5. Зв'язки між типами (Type relationships)
Порівняння двох типів та перевірка спадкування чи сумісності.

```cpp
#include <type_traits>

class Base {};
class Derived : public Base {};

static_assert(std::is_same_v<int, int> && !std::is_same_v<int, const int>);
static_assert(std::is_base_of_v<Base, Derived>);
static_assert(std::is_convertible_v<Derived*, Base*> && std::is_convertible_v<int, double>);
```

## 6. Трансформації типів (Type modifications)
Ці шаблони змінюють переданий тип через модифікатор `_t`.

```cpp
#include <type_traits>

void process_types() {
    // 1. Робота з CV-кваліфікаторами
    using T1 = std::remove_const_t<const int>;    // int
    using T2 = std::remove_volatile_t<volatile int>; // int
    using T3 = std::remove_cv_t<const volatile int>; // int
    using T4 = std::add_const_t<int>;             // const int
    using T5 = std::add_volatile_t<int>;          // volatile int
    using T6 = std::add_cv_t<int>;                // const volatile int

    // 2. Робота з посиланнями та вказівниками
    using T7 = std::remove_reference_t<int&>;     // int
    using T8 = std::add_lvalue_reference_t<int>;  // int&
    using T9 = std::add_rvalue_reference_t<int>;  // int&&
    using T10 = std::remove_pointer_t<int*>;      // int
    using T11 = std::add_pointer_t<int>;          // int*

    // 3. Спеціальні трансформації
    using T12 = std::decay_t<const int&>;         // int (імітує передачу за значенням)
    using T13 = std::common_type_t<int, double>;  // double (спільний тип для обох)
}
```

## 7. Логічні операції та умовні трейти
Важливі інструменти для побудови розгалужень у метапрограмуванні.

```cpp
#include <type_traits>

// std::conditional_t<Умова, ТипЯкщоTrue, ТипЯкщоFalse>
using Choice = std::conditional_t<sizeof(void*) == 8, long long, int>; 

// Логічні оператори (C++17)
static_assert(std::conjunction_v<std::is_integral<int>, std::is_signed<int>>); // AND
static_assert(std::disjunction_v<std::is_integral<float>, std::is_floating_point<float>>); // OR
static_assert(std::negation_v<std::is_void<int>>); // NOT
```

---

## 8. Реальний комплексний приклад використання у gnu++17

Поєднання `std::enable_if_t`, `std::is_base_of_v` та `std::is_arithmetic_v` для обмеження шаблонів функцій (SFINAE).

```cpp
#include <iostream>
#include <type_traits>

// Базовий клас для нашої ієрархії "Value"
struct BaseValue {};
struct ConcreteValue : BaseValue {};
struct WrongValue {};

// Клас-контейнер з обмеженням на рівні шаблону
template <typename Key, typename Value, typename = std::enable_if_t<std::is_base_of_v<BaseValue, Value>>>
class MyMap {};

// Функція, яка приймає ТІЛЬКИ числові типи (int, float, double тощо)
template <typename T, typename = std::enable_if_t<std::is_arithmetic_v<T>>>
void print_numeric(T val) {
    std::cout << "Number: " << val << std::endl;
}

int main() {
    // 1. Перевірка контейнера
    MyMap<int, ConcreteValue> ok_map;   // Компілюється
    // MyMap<int, WrongValue> bad_map;  // ПОМИЛКА: WrongValue не наслідує BaseValue

    // 2. Перевірка функції
    print_numeric(42);    // Компілюється (int)
    print_numeric(3.14);  // Компілюється (double)
    // print_numeric("str"); // ПОМИЛКА: const char* не є арифметичним типом
}
```
