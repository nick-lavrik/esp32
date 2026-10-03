#!/usr/bin/env bash
# Прогоняє тести lib/ButtonEvents на ХОСТІ, звичайним g++ (за зразком
# test/dino_game/run.sh). Клас не залежить від Arduino - пін читає
# src/Input/Buttons.cpp, тож заглушки не потрібні.
#
# Використання: ./test/button_events/run.sh
set -eu
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
OUT="$ROOT/.cache/test/button_events"
mkdir -p "$OUT"

g++ -std=gnu++17 -Wall -Wextra \
    -I "$ROOT/lib/ButtonEvents" \
    -o "$OUT/test_button_events" \
    "$HERE/test_button_events.cpp" "$ROOT/lib/ButtonEvents/ButtonEvents.cpp"

"$OUT/test_button_events"
