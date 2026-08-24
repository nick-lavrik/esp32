#!/usr/bin/env python3
"""Надсилає команду в SerialCommander плати і друкує вивід.

Використання: sdcmd.py "<команда>" [секунди_очікування]
Порт мусить бути вільний (закритий pio-монітор).
"""
import sys, os, time, serial

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


BAUD = 115200


def find_port():
    """Порт плати: з SD_PORT, з аргументу --port, або автопошуком.

    НАВІЩО АВТОПОШУК: на ESP32-S3 у режимі OTG плата віддається хосту
    композитним пристроєм (Mass Storage + CDC), і CDC отримує НАСТУПНИЙ
    вільний номер - ttyACM1 замість ttyACM0, бо нульовий уже зайнятий. Жорстко
    прописаний порт після кожного перепідключення вказував би не туди.
    """
    import glob

    if "--port" in sys.argv:
        return sys.argv[sys.argv.index("--port") + 1]
    if os.environ.get("SD_PORT"):
        return os.environ["SD_PORT"]

    candidates = sorted(glob.glob("/dev/ttyACM*") + glob.glob("/dev/ttyUSB*"))
    if not candidates:
        raise SystemExit("порт плати не знайдено (немає /dev/ttyACM* чи /dev/ttyUSB*)")

    # Пробуємо кожен: справжній порт плати відповідає на порожній рядок
    # запрошенням SerialCommander або будь-яким логом.
    for cand in candidates:
        try:
            probe = serial.Serial(cand, BAUD, timeout=0.4, dsrdtr=False, rtscts=False)
        except (serial.SerialException, OSError):
            continue
        try:
            probe.reset_input_buffer()
            probe.write(b"list\n")
            probe.flush()
            time.sleep(1.2)
            data = probe.read(8192)
        finally:
            probe.close()
        if b"[I][cmd" in data or b"Commands:" in data:
            return cand

    # Нічого не відповіло - віддаємо останній (найновіший) і хай викличний код
    # сам покаже, що тиша.
    return candidates[-1]

args = [a for a in sys.argv[1:] if not a.startswith("--")]
if "--port" in sys.argv:
    args = [a for a in args if a != sys.argv[sys.argv.index("--port") + 1]]
cmd = args[0] if args else "list"
wait = float(args[1]) if len(args) > 1 else 4.0

# dsrdtr/rtscts вимкнені: на ESP32-C6 (USB-Serial/JTAG) тіпання DTR/RTS
# перезавантажує плату або вішає порт.
PORT = find_port()
ser = serial.Serial(PORT, BAUD, timeout=0.2, dsrdtr=False, rtscts=False)
time.sleep(0.3)
ser.reset_input_buffer()
ser.write((cmd + "\n").encode())
ser.flush()

deadline = time.time() + wait
buf = b""
while time.time() < deadline:
    chunk = ser.read(4096)
    if chunk:
        buf += chunk
        deadline = max(deadline, time.time() + 1.0)  # продовжуємо, поки дані йдуть
ser.close()
sys.stdout.write(buf.decode("utf-8", errors="replace"))
