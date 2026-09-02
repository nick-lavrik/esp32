#include "DinoGame.hpp"

#include <math.h>

namespace {

float randRange(float lo, float hi) {
  if (hi <= lo) return lo;
  // random() з Arduino: цілочисельний [lo, hi). Множник 1000 дає крок 1 мс
  // ходу - для проміжків між кактусами цього більш ніж досить.
  const long span = (long)((hi - lo) * 1000.0f);
  if (span <= 0) return lo;
  return lo + (float)random(span) / 1000.0f;
}

}  // namespace

void DinoGame::begin(const DinoLayout &layout, const DinoTuning &tuning) {
  _layout = layout;
  _tuning = tuning;

  if (!_layout.valid()) {
    _logger.error("begin() got invalid layout (%dx%d, player %dx%d, kinds %u, apex %d)", (int)_layout.viewW,
                  (int)_layout.viewH, (int)_layout.playerW, (int)_layout.playerH, (unsigned)_layout.obstacleCount,
                  (int)_layout.jumpApex);
    return;
  }

  const float w = (float)_layout.viewW;
  _startSpeed = _tuning.startSpeedRatio * w;
  _maxSpeed = _tuning.maxSpeedRatio * w;
  _speedGain = _tuning.speedGainPerSec * w;
  _runStepPx = _tuning.runStepRatio * w;
  if (_runStepPx < 1.0f) _runStepPx = 1.0f;

  // Балістика. Гравітація - з двох відомих: висота апексу H і час підйому T
  // (g = 2H/T^2). А ось v0 = 2H/T тут БУЛО Б ПОМИЛКОЮ: ця формула справедлива
  // для сталої гравітації, а поки гравець тримає кнопку, діє полегшена
  // (holdGravityScale). Перевірено на хості: зі старим v0 стрибок з
  // утриманням злітав на 126 px замість 82, тобто діно заходило під HUD.
  //
  // Тому v0 шукаємо з умови «утримання до кінця дає РІВНО jumpApex»:
  //   H = v0*t1 - g1*t1^2/2 + (v0 - g1*t1)^2 / (2g),   g1 = holdGravityScale*g
  // тобто квадратне рівняння v0^2 + b*v0 + c = 0 з коефіцієнтами нижче.
  // Короткий тап при цьому дає ~60% апексу - саме та керована висота, що й
  // в оригіналі: малий кактус перестрибується тапом, великий вимагає утримання.
  const float H = (float)_layout.jumpApex;
  const float T = _tuning.jumpRiseSec > 0.01f ? _tuning.jumpRiseSec : 0.01f;
  _gravity = 2.0f * H / (T * T);

  const float t1 = _tuning.holdExtraSec;
  const float s = _tuning.holdGravityScale;
  const float g1 = s * _gravity;
  const float b = 2.0f * t1 * _gravity * (1.0f - s);
  const float c = g1 * g1 * t1 * t1 - _gravity * g1 * t1 * t1 - 2.0f * _gravity * H;
  const float disc = b * b - 4.0f * c;
  _jumpV0 = disc > 0.0f ? (-b + sqrtf(disc)) * 0.5f : 2.0f * H / T;

  // Мінімальна гарантована висота стрибка. Обрізання підйому (releaseJump)
  // не має опускати стрибок нижче за неї, інакше найкоротший тап не
  // перестрибував би навіть найнижчу перешкоду - гравець тицяє і все одно
  // врізається, хоча зробив усе правильно. Керованість висоти від цього не
  // страдає: стеля (jumpApex) лишається помітно вищою.
  int16_t lowest = _layout.obstacleH[0];
  for (uint8_t i = 1; i < _layout.obstacleCount && i < DinoLayout::kMaxKinds; ++i) {
    if (_layout.obstacleH[i] < lowest) lowest = _layout.obstacleH[i];
  }
  _minJumpH = (float)lowest * 1.25f;
  if (_minJumpH > H) _minJumpH = H;

  _logger.info("layout %dx%d ground=%d player=%dx%d apex=%d kinds=%u", (int)_layout.viewW, (int)_layout.viewH,
               (int)_layout.groundY, (int)_layout.playerW, (int)_layout.playerH, (int)_layout.jumpApex,
               (unsigned)_layout.obstacleCount);
  reset();
}

void DinoGame::reset() {
  _state = DinoState::Ready;
  _player = DinoPlayer{};
  for (uint8_t i = 0; i < kMaxObstacles; ++i) _obstacles[i] = DinoObstacle{};

  _elapsedSec = 0.0f;
  _speed = _startSpeed;
  _scrollPx = 0.0f;
  _scoreAcc = 0.0f;
  _score = 0;
  _jumpHeld = false;
  _pendingPress = false;
  _jumpStartMs = 0;
  _gameOverMs = 0;
  _lastMs = 0;  // 0 = «часу ще не було»: перший update() лише засікає момент
  scheduleNextSpawn();
}

void DinoGame::pressJump(uint32_t nowMs) {
  (void)nowMs;
  // Лише піднімаємо прапорці. Розбір - в update(), щоб уся зміна стану
  // відбувалась в одній точці й рівно раз на кадр.
  _pendingPress = true;
  _jumpHeld = true;
}

void DinoGame::releaseJump(uint32_t nowMs) {
  (void)nowMs;
  _jumpHeld = false;

  // Обрізаємо підйом - це і дає керовану висоту стрибка: коротке торкання
  // піднімає діно нижче, ніж утримання. Але не нижче за _minJumpH: швидкість,
  // якої вистачає долетіти з поточної висоти до цього мінімуму, лишається
  // недоторканою.
  if (_state == DinoState::Running && _player.vy > 0.0f) {
    float vy = _player.vy * _tuning.cutRiseScale;
    const float remaining = _minJumpH - _player.y;
    if (remaining > 0.0f) {
      const float vyKeep = sqrtf(2.0f * _gravity * remaining);
      if (vy < vyKeep) vy = vyKeep;
    }
    if (vy > _player.vy) vy = _player.vy;  // ніколи не ПІДКИДАЄМО
    _player.vy = vy;
  }
}

void DinoGame::startJump(uint32_t nowMs) {
  _player.vy = _jumpV0;
  _jumpStartMs = nowMs;
}

void DinoGame::scheduleNextSpawn() {
  const float gapSec = randRange(_tuning.gapMinSec, _tuning.gapMaxSec);
  float gapPx = _speed * gapSec;

  // Нижня межа в пікселях - страховка на найповільнішому старті: два кактуси
  // впритул неможливо перестрибнути одним стрибком незалежно від таймінгів.
  const float minGap = (float)_layout.playerW * 3.0f;
  if (gapPx < minGap) gapPx = minGap;
  _distToSpawn = gapPx;
}

void DinoGame::spawnObstacle() {
  for (uint8_t i = 0; i < kMaxObstacles; ++i) {
    if (_obstacles[i].active) continue;
    _obstacles[i].active = true;
    _obstacles[i].kind = (uint8_t)random(_layout.obstacleCount);
    _obstacles[i].x = (float)_layout.viewW;
    return;
  }
  // Вільних слотів немає - пропускаємо спавн. Це не помилка: kMaxObstacles
  // підібраний з запасом, а мовчазний пропуск кращий за витіснення кактуса,
  // на який гравець уже націлився.
}

void DinoGame::update(uint32_t nowMs) {
  if (!_layout.valid()) return;

  if (_lastMs == 0) {
    _lastMs = nowMs;
    return;  // перший кадр лише засікає час, інакше dt = вся аптайм-мітка
  }

  float dt = (float)(nowMs - _lastMs) / 1000.0f;
  _lastMs = nowMs;
  if (dt < 0.0f) dt = 0.0f;
  if (dt > _tuning.maxStepSec) dt = _tuning.maxStepSec;  // див. DinoTuning::maxStepSec

  // --- Ввід ---
  if (_pendingPress) {
    _pendingPress = false;
    switch (_state) {
      case DinoState::Ready:
        _state = DinoState::Running;
        startJump(nowMs);  // перше торкання одразу стрибає, як в оригіналі
        break;
      case DinoState::GameOver:
        if (nowMs - _gameOverMs > _tuning.restartLockMs) {
          reset();
          _state = DinoState::Running;
          _lastMs = nowMs;
        }
        break;
      case DinoState::Running:
        if (_player.onGround()) startJump(nowMs);
        break;
    }
  }

  if (_state != DinoState::Running) return;

  // --- Швидкість ---
  _elapsedSec += dt;
  _speed = _startSpeed + _speedGain * _elapsedSec;
  if (_speed > _maxSpeed) _speed = _maxSpeed;

  // --- Стрибок ---
  if (!_player.onGround() || _player.vy > 0.0f) {
    float g = _gravity;
    // Полегшена гравітація, поки тримають і поки діно ще піднімається.
    if (_jumpHeld && _player.vy > 0.0f && (nowMs - _jumpStartMs) < (uint32_t)(_tuning.holdExtraSec * 1000.0f)) {
      g *= _tuning.holdGravityScale;
    }
    _player.vy -= g * dt;
    _player.y += _player.vy * dt;
    if (_player.y <= 0.0f) {
      _player.y = 0.0f;
      _player.vy = 0.0f;
    }
  }

  // --- Світ ---
  const float advance = _speed * dt;
  _scrollPx += advance;

  for (uint8_t i = 0; i < kMaxObstacles; ++i) {
    if (!_obstacles[i].active) continue;
    _obstacles[i].x -= advance;
    if (_obstacles[i].x + (float)_layout.obstacleW[_obstacles[i].kind] < 0.0f) {
      _obstacles[i].active = false;
    }
  }

  _distToSpawn -= advance;
  if (_distToSpawn <= 0.0f) {
    spawnObstacle();
    scheduleNextSpawn();
  }

  // --- Рахунок ---
  _scoreAcc += advance * (_tuning.scorePerScreen / (float)_layout.viewW);
  _score = (uint32_t)_scoreAcc;

  // --- Колізії ---
  const DinoRect me = playerBox().shrunk(_tuning.hitboxShrink);
  for (uint8_t i = 0; i < kMaxObstacles; ++i) {
    if (!_obstacles[i].active) continue;
    if (!me.intersects(obstacleBox(_obstacles[i]).shrunk(_tuning.hitboxShrink))) continue;

    _state = DinoState::GameOver;
    _gameOverMs = nowMs;
    if (_score > _highScore) {
      _highScore = _score;
      _highScoreDirty = true;  // збереже той, хто володіє NVS
    }
    _logger.info("game over, score %u (hi %u)", (unsigned)_score, (unsigned)_highScore);
    break;
  }
}

uint8_t DinoGame::runFrame() const {
  if (_state != DinoState::Running || !_player.onGround()) return 0;
  return (uint8_t)(((uint32_t)(_scrollPx / _runStepPx)) & 1u);
}

DinoRect DinoGame::playerBox() const {
  return DinoRect{(float)_layout.playerX, (float)_layout.groundY - (float)_layout.playerH - _player.y,
                  (float)_layout.playerW, (float)_layout.playerH};
}

DinoRect DinoGame::obstacleBox(const DinoObstacle &o) const {
  const float w = (float)_layout.obstacleW[o.kind];
  const float h = (float)_layout.obstacleH[o.kind];
  return DinoRect{o.x, (float)_layout.groundY - h, w, h};
}
