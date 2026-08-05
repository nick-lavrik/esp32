#include "TouchPointMapper.h"

TouchPointMapper::TouchPointMapper(const TouchScreenConfig &config) : _config(config) {}

/* void TouchPointMapper::setConfig(const TouchScreenConfig &config) {
    _config = config;
} */

TouchPoint TouchPointMapper::map(int x, int y) const { return map(TouchPoint{x : x, y : y}); }

TouchPoint TouchPointMapper::map(TouchPoint raw) const {
  // X => return map(min(max(rawX, TOUCH_MIN_X), TOUCH_MAX_X), TOUCH_MAX_X, TOUCH_MIN_X, 0, 320);
  // Y => return map(min(max(rawY, TOUCH_MIN_Y), TOUCH_MAX_Y), TOUCH_MAX_Y, TOUCH_MIN_Y, 0, 240);
  int rangeX = _config.rawMaxX - _config.rawMinX;
  int rangeY = _config.rawMaxY - _config.rawMinY;
  if (rangeX == 0) rangeX = 1;  // захист від ділення на нуль при неправильному конфізі
  if (rangeY == 0) rangeY = 1;

  // Лінійне масштабування з "сирого" діапазону в піксельний
  long nx = (long)(raw.x - _config.rawMinX) * _config.screenWidth / rangeX;
  long ny = (long)(raw.y - _config.rawMinY) * _config.screenHeight / rangeY;

  if (_config.swapXY) {
    long tmp = nx;
    nx = ny;
    ny = tmp;
  }

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
