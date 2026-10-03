#include "DinoScreen.hpp"

#include <Logger.hpp>

#include "App/AppGlobals.hpp"
#include "ScreenManager.hpp"

#if HAS_DINO_GAME

DinoRenderer& dinoRenderer() {
  static DinoRenderer renderer;
  return renderer;
}

bool DinoScreen::available() const { return dinoRenderer().ready(); }

void DinoScreen::enter() { dinoRenderer().game().reset(); }

void DinoScreen::leave() {
  // Рекорд міг лишитись незбереженим, якщо гру вимкнули раніше, ніж
  // відпрацював cron-таск (див. setupDinoGame()).
  DinoGame& game = dinoRenderer().game();
  if (!game.highScoreDirty()) return;
  configStorage.setInt(CFG_DINO_HIGHSCORE, (int32_t)game.highScore());
  game.clearHighScoreDirty();
}

void DinoScreen::drawStrip(bool frameStart) {
  // Фізика рухається лише на початку повного кадру, а сцена малюється
  // щосмуги - інакше кожна смуга показала б свою фазу руху.
  dinoRenderer().frame(frameStart);
  // Лічильник кадрів живе всередині loopFrameRate(), а той викликається
  // лише з drawSystemInfo() - тобто в ігровому режимі просто стояв би,
  // і зміряти FPS самої гри (те, заради чого він і потрібен) було б
  // неможливо. Тут викликаємо його рівно раз за ітерацію, як і там.
  display.loopFrameRate();
}

void DinoScreen::onButton(ButtonId id, ButtonEvent event, uint32_t nowMs, uint32_t heldMs) {
  if (event == ButtonEvent::Press) {
    dinoRenderer().game().pressJump(nowMs);
  } else if (event == ButtonEvent::Release) {
    dinoRenderer().game().releaseJump(nowMs);
  } else if (event == ButtonEvent::LongPress) {
    Screen::onButton(id, event, nowMs, heldMs);
  }
}

void DinoScreen::onTouch(TouchPoint p) {
  (void)p;
  dinoRenderer().game().pressJump(millis());
}

void DinoScreen::onRelease(TouchPoint p) {
  (void)p;
  dinoRenderer().game().releaseJump(millis());
}

void setupDinoGame() {
  DinoRenderer& renderer = dinoRenderer();
  if (!renderer.begin()) {
    Logger::warn("dino game disabled (renderer init failed)");
    return;
  }

  renderer.game().setHighScore((uint32_t)configStorage.getInt(CFG_DINO_HIGHSCORE, 0));

  // Рекорд пишемо не в момент game over, а окремим таском: запис у NVS
  // всередині кадру дав би помітний фриз саме тоді, коли гравець дивиться
  // на екран найуважніше.
  scheduler.addCronTask(1000, [&renderer]() {
    if (!renderer.game().highScoreDirty()) return;
    const uint32_t hi = renderer.game().highScore();
    configStorage.setInt(CFG_DINO_HIGHSCORE, (int32_t)hi);
    renderer.game().clearHighScoreDirty();
    Logger::info("dino: new high score %u", (unsigned)hi);
  });

  Logger::info("Dino game setup done (hi %u)", (unsigned)renderer.game().highScore());
}

#endif  // HAS_DINO_GAME

void registerDinoCommands(SerialCommander& commander) {
  commander.registerCommand("dino", "Chrome Dino game on screen: dino on|off|test", [](const String& args) {
#if HAS_DINO_GAME
    const bool inDino = !strcmp(screens.active().name(), "dino") || !strcmp(screens.active().name(), "dino-sprites");
    if (!dinoRenderer().ready()) {
      Logger::warn("dino: renderer not ready");
    } else if (args.equalsIgnoreCase("on")) {
      screens.request("dino");
      Logger::info("dino game ON");
    } else if (args.equalsIgnoreCase("off")) {
      if (inDino) screens.requestHome();
      Logger::info("dino game OFF");
    } else if (args.equalsIgnoreCase("test")) {
      screens.request("dino-sprites");
      Logger::info("dino: sprite sheet mode ON (dino off to leave)");
    } else if (args.length() != 0) {
      Logger::info("use: dino on|off|test");
    } else {
      const DinoGame& g = dinoRenderer().game();
      const DinoLayout& L = g.layout();
      Logger::info("dino: %s", inDino ? screens.active().name() : "OFF");
      Logger::info("  screen %dx%d, ground y=%d, dino %dx%d, jump %d px", (int)L.viewW, (int)L.viewH, (int)L.groundY,
                   (int)L.playerW, (int)L.playerH, (int)L.jumpApex);
      Logger::info("  obstacle kinds: %u (large cactus %s)", (unsigned)L.obstacleCount,
                   L.obstacleCount > 1 ? "on" : "off");
      Logger::info("  score %u, high %u, speed %d px/s", (unsigned)g.score(), (unsigned)g.highScore(), (int)g.speed());
      // Кадр збирається за splitCount() проходів loop(), тому ігрових
      // кадрів на секунду рівно стільки ж разів менше.
      const uint32_t lr = display.loopFrameRate();
      Logger::info("  loop %u/s -> game %u fps (%u strips per frame)", (unsigned)lr,
                   (unsigned)(lr / display.splitCount()), (unsigned)display.splitCount());
    }
#else
    (void)args;
    Logger::info("dino: display game not available on this board");
#endif
  });
}
