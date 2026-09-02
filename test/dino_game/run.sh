#!/usr/bin/env bash
# Прогоняє тести моделі гри Chrome Dino на ХОСТІ, звичайним g++.
#
# Чому не PlatformIO Test Runner: lib/DinoGame не залежить ні від дисплея, ні
# від мережі, ні від FreeRTOS - лише від Arduino.h (random/millis) і TLogger.
# Підмінити ці два заголовки заглушками дешевше, ніж прошивати плату, а
# зворотний зв'язок - секунда замість хвилини. Саме так знайшлися два дефекти
# балістики: стрибок з утриманням перевищував заявлений апекс на 50%, і
# найкоротший тап не перестрибував найнижчий кактус.
#
# Використання: ./test/dino_game/run.sh
set -eu
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
OUT="$(mktemp -d)"
trap 'rm -rf "$OUT"' EXIT

g++ -std=gnu++17 -Wall -Wextra \
    -I "$HERE/stubs" -I "$ROOT/lib/DinoGame" \
    -o "$OUT/test_dino_game" \
    "$HERE/test_dino_game.cpp" "$ROOT/lib/DinoGame/DinoGame.cpp"

"$OUT/test_dino_game"
