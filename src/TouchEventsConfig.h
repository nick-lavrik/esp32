#pragma once

// Конфігурація TouchEvents. Не вказані поля отримують дефолти,
// підібрані під екран 320x240 (типовий ST7789).
struct TouchEventsConfig {
    int screenWidth                  = 320;
    int screenHeight                 = 240;

    unsigned long holdThresholdMs    = 600;  // час утримання для onHold
    unsigned long dblClickIntervalMs = 300;  // макс. інтервал між тапами для onDblClick
    unsigned int  swipeMinDistancePx = 30;   // мін. дистанція, щоб зарахувати свайп
    unsigned long swipeMaxDurationMs = 700;  // макс. тривалість жесту-свайпу
    unsigned int  edgeZonePx         = 30;   // зона біля краю для swipeFromXxx
};
