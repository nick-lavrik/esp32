#!/usr/bin/env python3
"""Знімає РІВНО ОДНЕ завантаження плати - від ресету до "Ready".

Навіщо окремий інструмент. Критерій приймання етапів рефактора Journal
(docs/journal_plan.md) - вивід setup() не змінився. Щоб порівняння взагалі
мало сенс, два захвати мусять містити те саме вікно. Наївне "відкрити порт і
читати N секунд" цього не дає: у буфері лежить хвіст попередньої сесії, а в
кінці набігають рядки вже ПІСЛЯ setup() (мережа піднялась, MQTT відвалився).
Перший же спробуваний захват через це містив два завантаження поспіль.

Тому межі шукає сам скрипт: початок - ROM-маркер "rst:0x" після
програмного ресету, кінець - останній рядок setup() ("Ready. Enter").

DTR/RTS НЕ ЧІПАЄМО - саме зміна цих ліній ресетить ESP32-C6 апаратно (та сама
причина, що в ./esp). Ресет робимо командою "reboot", тобто програмно.

Використання:
    ./tools/capture_boot.py > docs/baseline_setup_<env>.txt
    ./tools/capture_boot.py --port /dev/ttyACM0 --timeout 30
"""

import argparse
import sys
import time

import serial

START = "rst:0x"                 # ROM друкує це одразу після ресету
END = "Ready. Enter"             # останній рядок setup() у src/main.cpp


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="/dev/ttyACM0")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--timeout", type=float, default=30.0)
    args = ap.parse_args()

    port = serial.Serial()
    port.port, port.baudrate, port.timeout = args.port, args.baud, 0.2
    port.dtr = None                      # не торкатись ліній: див. заголовок
    port.rts = None
    port.open()

    time.sleep(0.4)
    port.reset_input_buffer()            # викинути хвіст попередньої сесії
    port.write(b"reboot\n")
    port.flush()

    deadline = time.time() + args.timeout
    raw = b""
    while time.time() < deadline:
        raw += port.read(4096)
        text = raw.decode("utf-8", "replace")
        if START in text and END in text[text.index(START):]:
            break
    port.close()

    text = raw.decode("utf-8", "replace")
    if START not in text:
        sys.stderr.write("no reset marker %r - did the board reboot?\n" % START)
        return 1
    text = text[text.index(START):]
    if END not in text:
        sys.stderr.write("no %r within %.0fs - setup() did not finish\n"
                         % (END, args.timeout))
        return 1
    # Кінець - разом із рядком END: він теж частина виводу setup().
    end = text.index(END)
    end = text.find("\n", end)
    sys.stdout.write(text[:end + 1] if end >= 0 else text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
