#!/usr/bin/env python3
"""Розбирає сирий лог команди 'heap' у CSV для аналізу фрагментації в часі.

Навіщо. Дослідження краху esp32-c3 (`docs/tech_debt.md`, розділ 4:
"esp32-c3: фрагментація heap росте за час роботи EcoFlow") - потрібен ряд
вимірів free/largest block/min free ever за години-добу штатної роботи, а не
одна точка. Прошивка сама раз на 2 хв кладе команду 'heap' у чергу
(тимчасовий cron у main.cpp) - лишається зняти вивід і звести у таблицю.

Джерело - сире дзеркало консолі, зняте окремо (console-mqtt on на пристрої,
далі щось на кшталт):
    mosquitto_sub -h <broker> -t '<prefix>/console/<clientId>' \\
      | ts '%Y-%m-%d %H:%M:%.S' >> heap_watch.raw.log

Команда 'heap' (src/main.cpp) друкує РІВНО шість рядків на виклик, завжди в
цьому порядку: total, free, largest block, fragmentation, min free ever,
uptime. Тому один семпл збирається без вікна за часом - просто накопичуємо
поля, а на "uptime" (завжди останній) закриваємо рядок CSV.

Використання:
    ./tools/parse_heap_watch.py heap_watch.raw.log -o heap_watch.csv
"""

import argparse
import csv
import re
import sys

# Префікс, який додає ts: "2026-09-17 14:32:01.123456 ". Секунди можуть бути
# без дробової частини - формат %.S у ts не завжди дає однакову ширину.
TIMESTAMP_RE = re.compile(r"^(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}(?:\.\d+)?)\s+(.*)$")

# Тег лишається "heap" (SerialLogger доповнює його до 7 символів пробілами -
# lib/Journal/JournalEntry.cpp: "[%s][%-7s] ") - у регексі це не важливо,
# \s* покриває будь-яку кількість пробілів.
TAG_RE = re.compile(r"^\[[IWDE]\]\[heap\s*\]\s*(.*)$")

FIELDS = {
    "total": re.compile(r"^total\s*:\s*(\d+)\s*B"),
    "free": re.compile(r"^free\s*:\s*(\d+)\s*B\s*\((\d+)%"),
    "largest": re.compile(r"^largest block:\s*(\d+)\s*B"),
    "fragmentation": re.compile(r"^fragmentation:\s*(\d+)%"),
    "min_free_ever": re.compile(r"^min free ever:\s*(\d+)\s*B"),
    "uptime": re.compile(r"^uptime\s*:\s*(\d+)\s*s"),
}

CSV_HEADER = [
    "receipt_time",
    "total_bytes",
    "free_bytes",
    "free_pct",
    "largest_block_bytes",
    "fragmentation_pct",
    "min_free_ever_bytes",
    "device_uptime_s",
]


def parse(lines):
    sample = {}
    for raw_line in lines:
        m = TIMESTAMP_RE.match(raw_line)
        if not m:
            continue  # рядок без мітки часу - не з ts, пропускаємо
        timestamp, rest = m.groups()

        m = TAG_RE.match(rest)
        if not m:
            continue  # не рядок команди 'heap'
        body = m.group(1)

        if "total" not in sample:
            sample["receipt_time"] = timestamp

        for name, pattern in FIELDS.items():
            fm = pattern.match(body)
            if fm:
                sample[name] = fm.groups()
                break

        if "uptime" in sample:
            yield sample
            sample = {}


def to_row(sample):
    total = sample.get("total", (None,))[0]
    free, free_pct = sample.get("free", (None, None))
    largest = sample.get("largest", (None,))[0]
    fragmentation = sample.get("fragmentation", (None,))[0]
    min_free_ever = sample.get("min_free_ever", (None,))[0]
    uptime = sample.get("uptime", (None,))[0]
    return [
        sample.get("receipt_time", ""),
        total,
        free,
        free_pct,
        largest,
        fragmentation,
        min_free_ever,
        uptime,
    ]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("input", help="сирий лог (ts-мітки + дзеркало консолі), '-' для stdin")
    ap.add_argument("-o", "--output", default="heap_watch.csv")
    args = ap.parse_args()

    source = sys.stdin if args.input == "-" else open(args.input, "r", errors="replace")
    try:
        samples = list(parse(source))
    finally:
        if source is not sys.stdin:
            source.close()

    if not samples:
        print("no 'heap' samples found - перевір, що console-mqtt on і ts додає мітки часу",
              file=sys.stderr)
        return 1

    with open(args.output, "w", newline="") as out:
        writer = csv.writer(out)
        writer.writerow(CSV_HEADER)
        writer.writerows(to_row(s) for s in samples)

    print(f"{len(samples)} sample(s) -> {args.output}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
