#include "TouchPointMapper.h"

TouchPointMapper::TouchPointMapper(const TouchScreenConfig &config) : _config(config) {}

/* void TouchPointMapper::setConfig(const TouchScreenConfig &config) {
    _config = config;
} */

TouchPoint TouchPointMapper::map(int x, int y) const { return map(TouchPoint{x : x, y : y}); }

TouchPoint TouchPointMapper::map(TouchPoint raw) const {
  // X => return map(min(max(rawX, TOUCH_MIN_X), TOUCH_MAX_X), TOUCH_MAX_X, TOUCH_MIN_X, 0, 320);
  // Y => return map(min(max(rawY, TOUCH_MIN_Y), TOUCH_MAX_Y), TOUCH_MAX_Y, TOUCH_MIN_Y, 0, 240);
  // Осі міняються місцями ДО масштабування - разом зі своїми сирими
  // діапазонами.
  //
  // Раніше swap стояв ПІСЛЯ масштабування, і це працювало лише на квадратній
  // панелі (4848s040, 480x480), де переставляти діапазони не було потреби.
  // На несиметричному екрані (esp32-c6: сирі 172x320 -> екран 320x172) той
  // порядок ламався: сира X масштабувалась у screenWidth (320) замість 172,
  // а інверсія й обрізання нижче застосовували screenWidth/screenHeight уже
  // до ПЕРЕСТАВЛЕНИХ осей - тобто nx обрізався до 171 і права частина екрана
  // ставала недосяжною.
  long rawX = raw.x;
  long rawY = raw.y;
  int minX = _config.rawMinX, maxX = _config.rawMaxX;
  int minY = _config.rawMinY, maxY = _config.rawMaxY;

  if (_config.swapXY) {
    long tmpRaw = rawX;
    rawX = rawY;
    rawY = tmpRaw;

    int tmpMin = minX, tmpMax = maxX;
    minX = minY;
    maxX = maxY;
    minY = tmpMin;
    maxY = tmpMax;
  }

  int rangeX = maxX - minX;
  int rangeY = maxY - minY;
  if (rangeX == 0) rangeX = 1;  // захист від ділення на нуль при неправильному конфізі
  if (rangeY == 0) rangeY = 1;

  // Лінійне масштабування з "сирого" діапазону в піксельний
  long nx = (rawX - minX) * _config.screenWidth / rangeX;
  long ny = (rawY - minY) * _config.screenHeight / rangeY;

  if (_config.invertX) nx = _config.screenWidth - 1 - nx;
  if (_config.invertY) ny = _config.screenHeight - 1 - ny;

  // Захист від виходу за межі екрана через шум сенсора
  if (nx < 0) nx = 0;
  if (ny < 0) ny = 0;
  if (nx >= _config.screenWidth) nx = _config.screenWidth - 1;
  if (ny >= _config.screenHeight) ny = _config.screenHeight - 1;

  /*
  TouchPoint result;
  result.x = (int) nx;
  result.y = (int) ny;
  return result;
  */
  return TouchPoint{x : (int)nx, y : (int)ny};
}
