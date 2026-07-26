#pragma once

#include <cstdint>

#include "Process.hpp"

/**
 * @brief Приклад класу-задачі для Process::doAsyncApp<float>().
 *
 * Кооперативно (БЕЗ власної задачі, БЕЗ delay()/vTaskDelay()) опитує
 * аналоговий пін кожні intervalMs мілісекунд, публікуючи кожне прочитане
 * значення через ctx.reportLatest(). Завершується після заданої кількості
 * вимірів; фінальний результат - середнє арифметичне.
 *
 * Кожен виклик operator() відбувається всередині Process::update() і МАЄ
 * бути дуже коротким (без блокувань) - інакше "заморозить" loop() та всі
 * інші кооперативні процеси.
 *
 * @code
 * ProcessHandle<float> h = Process::doAsyncApp<float>(SensorPollApp(A0, 300, 20));
 * // і не забути викликати Process::update() у loop()
 * @endcode
 */
class SensorPollApp {
public:
    SensorPollApp(uint8_t analogPin, uint32_t intervalMs, int sampleCount);

    /// Викликається з Process::update(). НЕ повинен блокувати виконання.
    int operator()(ProcessContext<float>& ctx);

private:
    uint8_t  _pin;
    uint32_t _intervalMs;
    int      _sampleCount;

    int      _samplesTaken = 0;
    float    _sum          = 0.0f;
    uint32_t _lastSampleAt = 0;
    bool     _started      = false;
};
