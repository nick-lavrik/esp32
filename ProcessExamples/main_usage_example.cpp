#include <Arduino.h>

#include "BlinkTask.hpp"
#include "Process.hpp"
#include "SensorPollApp.hpp"

// Один blink на "основному" світлодіоді + сенсор.
ProcessHandle<int>   blinkHandle;
ProcessHandle<int>   blink2Handle;
ProcessHandle<float> sensorHandle;

// Декілька одночасних BlinkTask на різних пінах (демонстрація динамічного пулу).
std::vector<ProcessHandle<int>> multiBlinkHandles;
bool multiBlinkReported = false;

void setup() {
    Serial.begin(115200);

    ProcessTaskOptions blinkOpts;
    blinkOpts.name      = "blinkTask";
    blinkOpts.stackSize = 2048;
    blinkOpts.priority  = 1;

    // --- Клас-задача для doAsyncTask (власна FreeRTOS-задача) ---
    blinkHandle = Process::doAsyncTask<int>(
        BlinkTask(/*pin=*/2, /*times=*/10, /*intervalMs=*/150),
        blinkOpts);

    // --- Клас-задача для doAsyncTask (власна FreeRTOS-задача) ---
    blink2Handle = Process::doAsyncTask<int>(
        BlinkTask(/*pin=*/2, /*times=*/10, /*intervalMs=*/150),
        ProcessTaskOptions{.name = "blinkTask", .stackSize = 2048, .priority = 1});

    // --- Клас-задача для doAsyncApp (кооперативна модель) ---
    sensorHandle = Process::doAsyncApp<float>(
        SensorPollApp(/*analogPin=*/A0, /*intervalMs=*/300, /*sampleCount=*/20));

    // --- Декілька одночасних doAsyncTask-процесів одного класу ---
    const uint8_t pins[] = {4, 5, 18};
    for (uint8_t pin : pins) {
        ProcessTaskOptions opts;
        opts.name      = "blinkMulti";
        opts.stackSize = 2048;
        multiBlinkHandles.push_back(
            Process::doAsyncTask<int>(BlinkTask(pin, /*times=*/5, /*intervalMs=*/100), opts));
    }
}

void loop() {
    // ОБОВ'ЯЗКОВО для будь-яких doAsyncApp-процесів (у т.ч. sensorHandle вище).
    Process::update();

    // --- Прогрес/результат blinkHandle ---
    int percent;
    while (blinkHandle.tryGetProgress(percent)) {
        Serial.printf("[Blink] progress: %d%%\n", percent);
    }
    if (blinkHandle.status() == ProcessStatus::Completed) {
        int total;
        if (blinkHandle.tryGetResult(total)) {
            Serial.printf("[Blink] finished, blinks: %d\n", total);
        }
    }

    // --- Прогрес/результат sensorHandle ---
    float sample;
    while (sensorHandle.tryGetProgress(sample)) {
        Serial.printf("[Sensor] value: %.1f\n", sample);
    }
    if (sensorHandle.status() == ProcessStatus::Completed) {
        float avg;
        if (sensorHandle.tryGetResult(avg)) {
            Serial.printf("[Sensor] average: %.2f\n", avg);
        }
    }

    // --- Одночасні multiBlinkHandles: чекаємо, доки всі завершаться ---
    if (!multiBlinkReported) {
        bool allDone = true;
        for (auto& h : multiBlinkHandles) {
            if (!h.isDone()) {
                allDone = false;
                break;
            }
        }
        if (allDone) {
            Serial.println("[MultiBlink] all parallel processes finished:");
            for (size_t i = 0; i < multiBlinkHandles.size(); ++i) {
                int result = 0;
                multiBlinkHandles[i].tryGetResult(result);
                Serial.printf("  task %u -> %d blinks\n", (unsigned)i, result);
            }
            multiBlinkReported = true;
        }
    }
}
