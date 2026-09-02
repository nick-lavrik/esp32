#pragma once

#include <stdint.h>

// Стан ігрового автомата.
//
//   Ready    - діно стоїть, світ не рухається, чекаємо першого натискання
//   Running  - гра йде
//   GameOver - зіткнення; рестарт дозволений не одразу (DinoTuning::restartLockMs),
//              інакше те саме натискання, яким гравець намагався стрибнути,
//              миттєво почало б новий забіг
enum class DinoState : uint8_t { Ready, Running, GameOver };
