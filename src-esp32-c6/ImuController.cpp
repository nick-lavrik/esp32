#include <ImuController.h>

#include <Logger.hpp>

#include "Qmi8658.h"

// Реалізація ImuController для esp32-c6 (QMI8658A на спільній з тачем
// I2C-шині). Підключається через build_src_filter, див. ImuController.h.

namespace {

Qmi8658 imu;

ImuController::Orientation g_orientation = ImuController::Orientation::Unknown;
float g_accel[3] = {0.0f, 0.0f, 0.0f};  // X, Y, Z у g

// Вісь, уздовж якої напрямлений ВЕРХ зображення, і знак цієї осі.
//
// Обидва параметри не вгадуються наперед - залежать від того, як чип
// розпаяний і повернутий на конкретній платі. Правильну вісь видно з команди
// "imu": треба повернути плату на 180 градусів у площині екрана і подивитись,
// яка з трьох осей ЗМІНИЛА ЗНАК (а не яка більша за модулем). На
// ESP32-C6-Touch-LCD-1.47 це X: при такому повороті він іде з -0.4 у +1.0,
// тоді як Z майже не рухається - тому початковий вибір Z був хибний і давав
// реакцію на зовсім інший рух (перевертання екраном донизу).
#ifndef IMU_UP_AXIS
#define IMU_UP_AXIS 0  // 0=X, 1=Y, 2=Z
#endif
static_assert(IMU_UP_AXIS >= 0 && IMU_UP_AXIS <= 2, "IMU_UP_AXIS must be 0(X), 1(Y) or 2(Z)");

// Знак впливає ЛИШЕ на те, яке з двох положень зветься TopUp у логах:
// сам flip реагує на ЗМІНУ орієнтації, а стартове положення береться за
// базове, тому екран перевертається правильно навіть за хибного знака.
#ifndef IMU_UP_AXIS_SIGN
#define IMU_UP_AXIS_SIGN (-1)
#endif
static_assert(IMU_UP_AXIS_SIGN == 1 || IMU_UP_AXIS_SIGN == -1,
              "IMU_UP_AXIS_SIGN must be 1 or -1");

// Опитувати частіше немає сенсу: переворот плати рукою - подія масштабу
// сотень мілісекунд, а кожне читання це I2C-транзакція на шині, спільній
// з тачем (який опитується щоцикл і має пріоритет за чуйністю).
constexpr uint32_t POLL_INTERVAL_MS = 100;

// Гістерезис. Між -0.6g і +0.6g орієнтація НЕ змінюється - це "мертва зона"
// для випадків, коли плату тримають вертикально або кладуть набік: там Z
// близький до нуля і без гістерезису стан хаотично скакав би.
constexpr float THRESHOLD_G = 0.6f;

// Скільки поспіль вимірів мають дати ту саму орієнтацію, щоб її прийняти.
// 5 * 100 мс = 0.5 с - достатньо, щоб відсіяти струс під час покладання
// плати на стіл, і непомітно для людини, яка свідомо її перевертає.
constexpr uint8_t STABLE_SAMPLES = 5;

ImuController::Orientation g_candidate = ImuController::Orientation::Unknown;
// ImuController::Orientation g_candidate = ImuController::Orientation::TopDown;
uint8_t g_stableCount = 0;
uint32_t g_lastPollMs = 0;

}  // namespace

bool ImuController::setup() {
  if (!imu.begin()) {
    Logger::warn("[IMU] QMI8658 not found on I2C (check: i2cscan)");
    return false;
  }

  // Перше читання одразу, щоб стартова орієнтація була відома вже в setup(),
  // а не через півсекунди після старту loop().
  float x = 0, y = 0, z = 0;
  if (imu.readAccel(x, y, z)) {
    g_accel[0] = x; g_accel[1] = y; g_accel[2] = z;
    const float up = g_accel[IMU_UP_AXIS] * IMU_UP_AXIS_SIGN;
    if (up > THRESHOLD_G) {
      g_orientation = Orientation::TopUp;
    } else if (up < -THRESHOLD_G) {
      g_orientation = Orientation::TopDown;
    }
    g_candidate = g_orientation;
    g_stableCount = STABLE_SAMPLES;
    Logger::info("[IMU] QMI8658 ready: X=%.2f Y=%.2f Z=%.2f g | axis %s -> %s",
                 x, y, z, upAxisName(), orientationName(g_orientation));
  }
  return true;
}

void ImuController::update() {
  if (!imu.isPresent()) return;

  const uint32_t now = millis();
  if (now - g_lastPollMs < POLL_INTERVAL_MS) return;
  g_lastPollMs = now;

  float x = 0, y = 0, z = 0;
  if (!imu.readAccel(x, y, z)) return;
  g_accel[0] = x; g_accel[1] = y; g_accel[2] = z;
  const float up = g_accel[IMU_UP_AXIS] * IMU_UP_AXIS_SIGN;

  // Поза порогами - у мертвій зоні; лічильник стабільності скидаємо, але
  // саму орієнтацію лишаємо попередньою.
  Orientation sample;
  if (up > THRESHOLD_G) {
    sample = Orientation::TopUp;
  } else if (up < -THRESHOLD_G) {
    sample = Orientation::TopDown;
  } else {
    g_stableCount = 0;
    g_candidate = Orientation::Unknown;
    return;
  }

  if (sample != g_candidate) {
    g_candidate = sample;
    g_stableCount = 1;
    return;
  }

  if (g_stableCount < STABLE_SAMPLES) {
    ++g_stableCount;
    if (g_stableCount == STABLE_SAMPLES) {
      g_orientation = g_candidate;
    }
  }
}

ImuController::Orientation ImuController::orientation() { return g_orientation; }

float ImuController::accelX() { return g_accel[0]; }
float ImuController::accelY() { return g_accel[1]; }
float ImuController::accelZ() { return g_accel[2]; }

float ImuController::upAxisValue() { return g_accel[IMU_UP_AXIS] * IMU_UP_AXIS_SIGN; }

const char* ImuController::upAxisName() {
  return (IMU_UP_AXIS == 0) ? "X" : (IMU_UP_AXIS == 1) ? "Y" : "Z";
}

const char* ImuController::orientationName(Orientation o) {
  switch (o) {
    case Orientation::TopUp: return "TopUp (top side up)";
    case Orientation::TopDown: return "TopDown (rotated 180)";
    default: return "Unknown";
  }
}
