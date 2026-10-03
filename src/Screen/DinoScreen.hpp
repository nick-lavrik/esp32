#pragma once

// Гра Chrome Dino (HAS_DINO_GAME). Фізика й рахунок - lib/DinoGame, рендер -
// src/Dino/DinoRenderer.
//
// Кнопка/тач: натиск - стрибок, відпускання обрізає підйом (керована висота),
// кнопка 3 с - вихід на основний екран. Double-click тут НЕ перемикає екран:
// два швидкі стрибки - звичайна гра, а не жест. realtime(): без doPing()/ecoflow.loop(),
// інакше кадр рветься (кактус "телепортується" крізь діно).
//
// Режим після ресету НЕ відновлюється - див. коментар у loadScreenSettings()
// (src/Screen/ScreenControl.cpp); рекорд зберігається (CFG_DINO_HIGHSCORE).

#include <SerialCommander.hpp>

#include "Screen.hpp"
#include "features.h"

#if HAS_DINO_GAME
#include "Dino/DinoRenderer.hpp"

class DinoScreen : public Screen {
public:
  const char* name() const override { return "dino"; }
  bool available() const override;
  void enter() override;
  void leave() override;
  void drawStrip(bool frameStart) override;
  bool realtime() const override { return true; }

  void onButton(ButtonId id, ButtonEvent event, uint32_t nowMs, uint32_t heldMs) override;
  // onTouch/onRelease, а НЕ onClick: onClick спрацьовує на відпусканні (і то
  // лише якщо не було hold чи свайпу), тобто стрибок або запізнювався б, або
  // не зараховувався взагалі при довгому натисканні.
  void onTouch(TouchPoint p) override;
  void onRelease(TouchPoint p) override;
};

// Рендерер гри - один на обидва dino-екрани.
DinoRenderer& dinoRenderer();

// Старт рендерера, рекорд з NVS, cron збереження рекорду.
void setupDinoGame();
#endif

// Команда 'dino on|off|test' - на всіх платах (список команд однаковий,
// недоступність видно з відповіді).
void registerDinoCommands(SerialCommander& commander);
