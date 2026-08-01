#pragma once


#define TOUCH_MIN_X 212
#define TOUCH_MAX_X 3714

#define TOUCH_MIN_Y 329
#define TOUCH_MAX_Y 3817

// Налаштування калібрування: сирий діапазон координат тачскріна -> розмір екрана.
// Типова причина розбіжності: контролер дотику (напр. XPT2046/GT911) видає
// координати у власному "сирому" діапазоні (0..4095 і т.п.), який не збігається
// з роздільною здатністю дисплея, або екран повернутий (rotation).
struct TouchScreenConfig {
    int rawMinX = TOUCH_MIN_X, rawMaxX = TOUCH_MAX_X;
    int rawMinY = TOUCH_MIN_Y, rawMaxY = TOUCH_MAX_Y;

    int screenWidth  = TFT_WIDTH;
    int screenHeight = TFT_HEIGHT;

    bool invertX = false;
    bool invertY = false;
    bool swapXY  = false; // для повороту екрана на 90/270 градусів

    // TouchEventsConfig
    unsigned long holdThresholdMs    = 600;  // час утримання для onHold
    unsigned long dblClickIntervalMs = 300;  // макс. інтервал між тапами для onDblClick
    unsigned int  swipeMinDistancePx = 30;   // мін. дистанція, щоб зарахувати свайп
    unsigned long swipeMaxDurationMs = 700;  // макс. тривалість жесту-свайпу
    unsigned int  edgeZoneX          = 30;   // зона біля краю для swipeFromXxx
    unsigned int  edgeZoneY          = 30;   // зона біля краю для swipeFromXxx
};
