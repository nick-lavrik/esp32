#pragma once

#include <Arduino.h>

#include <TLogger.hpp>

#include "DinoLayout.hpp"
#include "DinoObstacle.hpp"
#include "DinoPlayer.hpp"
#include "DinoRect.hpp"
#include "DinoState.hpp"
#include "DinoTuning.hpp"

// Модель гри Chrome Dino. НЕ знає ні про дисплей, ні про спрайти, ні про ввід -
// лише про геометрію, яку їй дали в DinoLayout.
//
//   DinoLayout layout = /* заповнює рендер за розміром екрана */;
//   DinoGame game;
//   game.begin(layout);
//   game.setHighScore(configStorage.getInt("dino.hi", 0));
//   ...
//   if (display.isFrameStart()) game.update(millis());   // РІВНО раз на кадр
//   render(game);                                        // читає геттери
//
// Одиниці: усередині - float-пікселі поточного екрана. Другої системи
// координат навмисно немає: константи з DinoTuning перераховуються в пікселі
// один раз у begin(), і далі жоден шлях виконання не потребує конвертації
// (а отже й не може її забути).
class DinoGame {
public:
  void begin(const DinoLayout &layout, const DinoTuning &tuning = DinoTuning{});
  void reset();

  // Крок фізики. Викликати РІВНО РАЗ на повний кадр - інакше кожна смуга
  // смугового рендера побачить свою фазу руху (див. Display::isFrameStart()).
  void update(uint32_t nowMs);

  // Ввід. Приходить із колбеків тача/кнопки, тобто МІЖ викликами update(),
  // тому лише зводить латч, а фізику не рухає.
  void pressJump(uint32_t nowMs);
  void releaseJump(uint32_t nowMs);

  DinoState state() const { return _state; }
  bool ready() const { return _layout.valid(); }

  const DinoPlayer &player() const { return _player; }
  const DinoObstacle *obstacles() const { return _obstacles; }
  static constexpr uint8_t maxObstacles() { return kMaxObstacles; }
  const DinoLayout &layout() const { return _layout; }

  float speed() const { return _speed; }
  float scrollPx() const { return _scrollPx; }

  // 0/1 - кадр анімації бігу. Похідна від пройденої ДИСТАНЦІЇ, а не від
  // millis(): рендер викликається кілька разів на кадр, і будь-яке звернення
  // до годинника всередині нього дало б різну ногу в різних смугах.
  uint8_t runFrame() const;

  uint32_t score() const { return _score; }
  uint32_t highScore() const { return _highScore; }
  void setHighScore(uint32_t value) { _highScore = value; }
  bool highScoreDirty() const { return _highScoreDirty; }
  void clearHighScoreDirty() { _highScoreDirty = false; }

  DinoRect playerBox() const;
  DinoRect obstacleBox(const DinoObstacle &o) const;

private:
  static constexpr uint8_t kMaxObstacles = 4;

  void spawnObstacle();
  void scheduleNextSpawn();
  void startJump(uint32_t nowMs);

  DinoLayout _layout{};
  DinoTuning _tuning{};
  DinoState _state = DinoState::Ready;

  DinoPlayer _player{};
  DinoObstacle _obstacles[kMaxObstacles]{};

  uint32_t _lastMs = 0;
  uint32_t _gameOverMs = 0;
  uint32_t _jumpStartMs = 0;
  bool _jumpHeld = false;
  bool _pendingPress = false;  // натиснули між кадрами - обробимо в update()

  float _elapsedSec = 0.0f;
  float _speed = 0.0f;
  float _scrollPx = 0.0f;
  float _distToSpawn = 0.0f;
  float _scoreAcc = 0.0f;

  // Похідні від layout/tuning, пораховані один раз у begin()
  float _startSpeed = 0.0f;
  float _maxSpeed = 0.0f;
  float _speedGain = 0.0f;
  float _jumpV0 = 0.0f;
  float _gravity = 0.0f;
  float _runStepPx = 1.0f;
  float _minJumpH = 0.0f;  // гарантований мінімум висоти стрибка

  uint32_t _score = 0;
  uint32_t _highScore = 0;
  bool _highScoreDirty = false;

  const TLogger _logger{"dino"};
};
