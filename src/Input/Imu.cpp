// IMU (BOARD_HAS_IMU): орієнтація плати -> переворот зображення.

#include <Arduino.h>

#include <Logger.hpp>

#include "App/AppGlobals.hpp"
#include "Input.hpp"
#include "Screen/ScreenControl.hpp"

#if BOARD_HAS_IMU
#include <ImuController.h>
#endif

void setupImu() {
#if BOARD_HAS_IMU
  if (ImuController::setup()) {
    Logger::info("IMU setup done");
  }
#endif
}

// Переворот плати догори дриґом перевертає й зображення, і навпаки.
//
// Стан порівнюється з ПОПЕРЕДНІМ, а не з абсолютною орієнтацією: flip()
// перемикає поточний поворот на 180 градусів, тому реагувати треба саме на
// ЗМІНУ, інакше кожен виклик у FaceDown крутив би екран нескінченно.
// Стартова орієнтація фіксується як базова і сама по собі flip не викликає -
// плата, увімкнена вже перевернутою, показує звичайний екран.
void updateImuFlip() {
#if BOARD_HAS_IMU
  static ImuController::Orientation last = ImuController::Orientation::Unknown;

  ImuController::update();
  const ImuController::Orientation now = ImuController::orientation();

  if (now == ImuController::Orientation::Unknown) return;

  if (last == ImuController::Orientation::Unknown && now == ImuController::Orientation::TopUp) {
    last = now;  // базова орієнтація зі старту, без flip
    return;
  }

  if (now == last) return;

  last = now;
  Logger::info("[IMU] orientation changed: %s (%s=%.2fg) -> flip", ImuController::orientationName(now),
               ImuController::upAxisName(), ImuController::upAxisValue());

  display_flip();
#endif
}

void registerImuCommands(SerialCommander& commander) {
#if BOARD_HAS_IMU
  commander.registerCommand("imu", "show IMU orientation and raw Z acceleration", [](const String& args) {
    Logger::info("IMU: %s | X=%.2f Y=%.2f Z=%.2f g | axis %s=%.2fg",
                 ImuController::orientationName(ImuController::orientation()), ImuController::accelX(),
                 ImuController::accelY(), ImuController::accelZ(), ImuController::upAxisName(),
                 ImuController::upAxisValue());
  });
#endif
  (void)commander;
}
