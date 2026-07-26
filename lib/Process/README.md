# Process — асинхронне (non-blocking) виконання коду для ESP32

PlatformIO / Arduino-ESP32 core 3.3.9 / ESP-IDF v5.5.4, gnu++17.

## Структура файлів

| Файл | Клас/тип |
|---|---|
| `ProcessStatus.hpp` | `enum class ProcessStatus` |
| `ProcessTaskOptions.hpp` | `struct ProcessTaskOptions` |
| `ProcessResultQueue.hpp` | `template<T> class ProcessResultQueue` |
| `ProcessState.hpp` | `template<T> class ProcessState` (внутрішній) |
| `ProcessContext.hpp` | `template<T> class ProcessContext` |
| `ProcessHandle.hpp` | `template<T> class ProcessHandle` |
| `Process.hpp` / `Process.cpp` | `class Process` (фасад) |

Шаблонні класи (`ProcessResultQueue`, `ProcessState`, `ProcessContext`, `ProcessHandle`)
реалізовані повністю в `.hpp`, бо шаблони неможливо коректно розділити на `.hpp`/`.cpp`
без явної інстанціації під конкретні типи. `Process` — не шаблон, тому має класичний
`.hpp`+`.cpp` поділ; шаблонні методи (`doAsyncTask`, `doAsyncApp`) визначені в `.hpp`,
але делегують важку роботу (реєстр, критичні секції) у нешаблонні приватні методи,
реалізовані в `.cpp`.

## Дві моделі виконання, один інтерфейс

Обидві моделі отримують **той самий** `ProcessContext<TProgress>&`:

```cpp
ctx.report(value);          // неблокуючий запис проміжного результату в чергу
ctx.reportLatest(value);    // те саме, але витісняє найстаріший елемент при переповненні
ctx.isCancelled();          // чи запросили скасування ззовні
ctx.finish(result);         // успішне завершення з фінальним результатом
ctx.acknowledgeCancel();    // підтвердити скасування
ctx.fail();                 // завершення з помилкою
```

### 1) `Process::doAsyncTask<T>()` — окрема FreeRTOS-задача

Справжня паралельність, в т.ч. між ядрами. Кожен виклик = нова задача
(`xTaskCreatePinnedToCore`), яка сама себе видаляє по завершенню.
Пул динамічний — обмежень на кількість немає (лімітується лише вільною RAM для стеків).

```cpp
#include "Process.hpp"

ProcessTaskOptions opts;
opts.name = "wifiScan";
opts.stackSize = 8192;   // байти
opts.priority = 2;
opts.coreId = 1;

ProcessHandle<int> handle = Process::doAsyncTask<int>(
    [](ProcessContext<int>& ctx) {
        for (int i = 0; i <= 100; i += 10) {
            if (ctx.isCancelled()) {
                ctx.acknowledgeCancel();
                return;
            }
            // ... якась довга робота (наприклад, WiFi.scanNetworks()) ...
            vTaskDelay(pdMS_TO_TICKS(200));
            ctx.reportLatest(i);  // проміжний прогрес у %
        }
        ctx.finish(100);
    },
    opts);

// десь у loop():
int progress;
while (handle.tryGetProgress(progress)) {
    Serial.printf("Прогрес: %d%%\n", progress);
}
if (handle.isDone()) {
    int result;
    if (handle.tryGetResult(result)) {
        Serial.printf("Завершено, результат = %d\n", result);
    }
}
```

### 2) `Process::doAsyncApp<T>()` — кооперативна модель

Жодної нової задачі не створюється. `updateFn` викликається один раз за
кожен виклик `Process::update()` (яку треба викликати з `loop()`).
Повертає `0` — продовжити наступного разу, `1` — завершено.

```cpp
int step = 0;

ProcessHandle<String> handle = Process::doAsyncApp<String>(
    [step](ProcessContext<String>& ctx) mutable -> int {
        if (ctx.isCancelled()) {
            ctx.acknowledgeCancel();
            return 1;
        }
        step++;
        ctx.reportLatest(String("крок ") + step);
        if (step >= 5) {
            ctx.finish("готово");
            return 1;
        }
        return 0;  // ще не завершено
    });

void loop() {
    Process::update();  // прокачує ВСІ зареєстровані doAsyncApp-процеси

    String msg;
    while (handle.tryGetProgress(msg)) {
        Serial.println(msg);
    }
}
```

## Одночасне виконання декількох процесів

Обидва методи повертають дешевий для копіювання `ProcessHandle<T>` —
зберігайте скільки завгодно хендлів (наприклад, у `std::vector<ProcessHandle<int>>`)
і опитуйте кожен незалежно. `Process::update()` сама прокачує всі зареєстровані
`doAsyncApp`-процеси одним викликом.

```cpp
std::vector<ProcessHandle<int>> tasks;
for (int i = 0; i < 5; ++i) {
    tasks.push_back(Process::doAsyncTask<int>([i](ProcessContext<int>& ctx) {
        vTaskDelay(pdMS_TO_TICKS(1000 * (i + 1)));
        ctx.finish(i * 10);
    }));
}
```

## Важливо: тип `TProgress`

`ProcessResultQueue<T>` використовує нативну FreeRTOS-чергу (`xQueueCreate`),
яка копіює дані через `memcpy`. Тому `T` **має бути trivially copyable**
(`int`, `float`, `struct { int a; char msg[32]; }` тощо).

Для "важких" типів (`String`, `std::vector`, довільні дані з власною купою):
не кладіть їх у чергу напряму. Використайте `T = MyType*`:
1. виробник: `auto* p = new MyType(...); ctx.report(p);`
2. споживач: прочитав вказівник → обов'язково `delete p;` одразу після використання.

## Скасування процесу

```cpp
handle.cancel();  // лише виставляє прапорець cancelRequested = true
```

Сама функція процесу відповідає за перевірку `ctx.isCancelled()` та коректне
завершення (`ctx.acknowledgeCancel()`). Без цього процес продовжить роботу —
це свідоме рішення (кооперативне скасування, без примусового вбивства задачі
посеред виконання, що небезпечно для утримуваних ресурсів/мьютексів).
