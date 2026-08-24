#!/usr/bin/env python3
"""Замінює SD_FREQ лише в блоці [env:esp32-c6] файлу platformio.ini."""
import re, sys

# --- шляхи -----------------------------------------------------------------
# Скрипти лежать у sd-rescue/tools/, робочі дані - у sd-rescue/data/.
# Шлях рахується від самого файлу, тому скрипт можна запускати з будь-якої
# теки, а перенесення проєкту нічого не ламає. SD_RESCUE_DATA дозволяє
# вказати іншу теку з даними (напр. на іншому диску).
import os as _os
from pathlib import Path as _Path
SP = _Path(_os.environ.get("SD_RESCUE_DATA",
                           _Path(__file__).resolve().parent.parent / "data"))
SP.mkdir(parents=True, exist_ok=True)


freq = sys.argv[1]
path = "/home/nick/Work/ESP32/esp32/platformio.ini"
lines = open(path).read().split("\n")

in_env = False
changed = 0
for i, line in enumerate(lines):
    if line.startswith("["):
        in_env = line.strip() == "[env:esp32-c6]"
    if in_env and re.match(r"\s*-D SD_FREQ=", line):
        lines[i] = re.sub(r"SD_FREQ=\d+", f"SD_FREQ={freq}", line)
        changed += 1

assert changed == 1, f"знайдено {changed} рядків SD_FREQ у [env:esp32-c6]"
open(path, "w").write("\n".join(lines))
print(f"SD_FREQ={freq}")
