#pragma once

// Налаштування калібрування: сирий діапазон координат тачскріна -> розмір екрана.
// Типова причина розбіжності: контролер дотику (напр. XPT2046/GT911) видає
// координати у власному "сирому" діапазоні (0..4095 і т.п.), який не збігається
// з роздільною здатністю дисплея, або екран повернутий (rotation).
struct TouchScreenConfig {
  int rawMinX = 0, rawMaxX = TFT_WIDTH;
  int rawMinY = 0, rawMaxY = TFT_HEIGHT;

  int screenWidth = TFT_WIDTH;
  int screenHeight = TFT_HEIGHT;

  bool invertX = false;
  bool invertY = false;
  bool swapXY = false;  // для повороту екрана на 90/270 градусів

  // TouchEventsConfig
  unsigned long holdThresholdMs = 600;  // час утримання для onHold
  unsigned long dblClickIntervalMs = 300;  // макс. інтервал між тапами для onDblClick
  unsigned int swipeMinDistancePx = 30;  // мін. дистанція, щоб зарахувати свайп
  unsigned long swipeMaxDurationMs = 700;  // макс. тривалість жесту-свайпу
  unsigned int edgeZoneX = 30;             // зона біля краю для swipeFromXxx
  unsigned int edgeZoneY = 30;             // зона біля краю для swipeFromXxx
};
