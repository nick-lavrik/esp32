#!/usr/bin/env python3
"""Нормалізує захоплений serial-лог, щоб два запуски можна було порівняти.

Навіщо. Критерій приймання етапу 1 рефактора Journal (docs/journal_plan.md) -
вивід setup() має лишитись тим самим. Порівняти сирі захвати не можна з трьох
причин:

  1. У потоці ДВА джерела: наш логер ("[I][tag    ] ...") і ESP-IDF
     ("[  7854][E][file.cpp:138] ..."). Другий має власний таймстемп, який
     різний щозапуску.
  2. Рядки ПЕРЕПЛІТАЮТЬСЯ посеред тексту - IDF пише напряму в UART, повз наш
     PrintQueue, тож його рядок може розрізати наш навпіл:
       [  7854][E][NetworkManager[I][ecoflow] mqtt-e... subscriptions: 8
       .cpp:138] hostByName(): DNS Failed ...
     (Саме це Journal і має прибрати - запис цілого рядка стає атомарним.)
  3. Багато значень мінливі за природою: heap, uptime, IP, RSSI, рекорд гри.

Тому: витягуємо ЛИШЕ рядки нашого логера (навіть якщо їх розрізало), потім
маскуємо мінливі числа. Те, що лишається, - структура виводу, і саме вона
має збігтись до байта.

Використання:
    ./tools/logdiff.py capture.txt                 # надрукувати нормалізоване
    ./tools/logdiff.py before.txt after.txt        # порівняти два захвати

Коди виходу при порівнянні: 0 - збігається, 2 - ті самі рядки в іншому порядку
(таке дає, напр., "MQTT connect fail", момент якого залежить від мережі),
1 - справжня різниця: рядок додався, зник або змінився.
"""

import re
import sys

# Наш рядок: "[I][tag    ] текст". Тег - до 10 символів, рівні IWEDV.
# У тегу заборонена '[': інакше обрізаний IDF-рядок
# ("[  7854][E][NetworkManager[I][mqtt   ] ...") дав би хибне співпадіння з
# тегом "Netwo[I" - саме так і сталось на першому прогоні.
OURS = re.compile(r"\[([IWEDV])\]\[([^\[\]\n]{1,10})\] ?")
# Початок будь-якого рядка-джерела: наш або IDF ("[  123][E][file.cpp:12] ").
ANY_START = re.compile(r"\[[IWEDV]\]\[[^\]\n]{1,10}\]|\[\s*\d+\]\[[EWIDV]\]\[")

# Завжди мінливе, незалежно від рядка.
ALWAYS = [
    (re.compile(r"\b\d{1,3}(?:\.\d{1,3}){3}\b"), "<ip>"),
    (re.compile(r"\b(?:[0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}\b"), "<mac>"),
    (re.compile(r"\b\d{2}:\d{2}:\d{2}\b"), "<time>"),
]

# Числа маскуємо НЕ всюди, а лише в рядках про мінливі за природою величини.
# Інакше нормалізація з'їдає корисне: пін-номери, розміри спрайта, порт
# - і діф перестає ловити регресії, заради яких існує.
VOLATILE_LINE = re.compile(
    r"heap|uptime|rssi|dBm|free|hi \d|took|ms\b|elapsed|millis", re.I)

# Окреме число (не частина слова): "322028" так, "ESP32"/"FAT32"/"lcd096" ні.
NUMBER = re.compile(r"(?<![A-Za-z0-9_])-?\d+(?![A-Za-z0-9_])")


def normalize(text):
    """Витягує рядки нашого логера і маскує мінливі значення."""
    out = []
    for m in OURS.finditer(text):
        rest = text[m.end():]
        # Текст рядка - до початку наступного джерела або до кінця рядка.
        nxt = ANY_START.search(rest)
        eol = rest.find("\n")
        end = min(x for x in (nxt.start() if nxt else len(rest),
                              eol if eol >= 0 else len(rest)) if x >= 0)
        body = rest[:end].rstrip()
        for pattern, repl in ALWAYS:
            body = pattern.sub(repl, body)
        if VOLATILE_LINE.search(body):
            body = NUMBER.sub("<n>", body)
        out.append(f"[{m.group(1)}][{m.group(2).strip()}] {body}".rstrip())
    return out


def read(path):
    with open(path, "rb") as handle:
        return handle.read().decode("utf-8", "replace")


def main(argv):
    if len(argv) == 2:
        print("\n".join(normalize(read(argv[1]))))
        return 0

    if len(argv) == 3:
        import difflib
        from collections import Counter
        a, b = normalize(read(argv[1])), normalize(read(argv[2]))
        diff = list(difflib.unified_diff(a, b, argv[1], argv[2], lineterm=""))
        if not diff:
            print(f"identical: {len(a)} normalized lines")
            return 0
        print("\n".join(diff))
        # Ті самі рядки в іншому порядку - НЕ те саме, що змінений вивід.
        # Реальний випадок: "MQTT connect fail" з'являється після конекту, і
        # його місце в потоці залежить від затримки мережі, а не від коду.
        # Тому такий діф віддаємо окремим кодом (2), щоб він не читався як
        # регресія: будь-який доданий, зниклий чи змінений рядок так само дає 1.
        if Counter(a) == Counter(b):
            moved = sum(1 for x, y in zip(a, b) if x != y)
            print(f"\n^ ті самі {len(a)} рядків, інший порядок ({moved} зсунуто)")
            return 2
        return 1

    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
