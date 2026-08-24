#!/usr/bin/env python3
"""Перегляд дерева файлів, знайденого walk_tree.py.

Дерево лежить у tree.jsonl - по рядку на запис. Цей скрипт нічого не читає з
картки чи образу, тільно з готового файлу, тому працює миттєво й ніяк не
заважає зніманню образу.

Використання:
    showtree.py                 # верхні два рівні
    showtree.py /home/nick      # вміст конкретної теки
    showtree.py /etc --depth 2  # з глибиною
    showtree.py --find passwd   # пошук за іменем
"""
import os, sys, json
from collections import Counter
from pathlib import Path

DATA = Path(os.environ.get("SD_RESCUE_DATA",
                           Path(__file__).resolve().parent.parent / "data"))
TREE = DATA / "tree.jsonl"

if not TREE.exists():
    raise SystemExit(f"немає {TREE} - спершу запусти walk_tree.py")

KIND = {"d": "тека", "f": "файл", "l": "link", "?": "?"}

# Позначки готовності. Свідомо не рахуємо готовність для всього дерева: щоб
# дізнатися, у яких блоках лежить файл, треба прочитати його inode, а це
# 105 тисяч читань на повне дерево. Для однієї теки - справа секунд, і саме
# так це й задумано: питання "чи можна вже витягати оце" завжди про теку.
MARK_READY, MARK_PARTIAL, MARK_ABSENT, MARK_UNKNOWN = "+", "~", "-", " "
ST_TODO = 0


def build_readiness(records, source, chunk, limit):
    """{шлях: позначка} для звичайних файлів зі списку."""
    import struct
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    from ext4http import CardReader, Ext4, DEFAULT_SOURCE

    src = source or DEFAULT_SOURCE
    state_path = Path(str(src) + ".state")
    state = state_path.read_bytes() if state_path.exists() else None

    files = [r for r in records if r["k"] == "f"]
    if len(files) > limit:
        print(f"(готовність: файлів {len(files)}, перевіряю перші {limit} - "
              f"решта без позначки; збільш --max-check, якщо треба)\n")
        files = files[:limit]

    reader = CardReader(str(src))
    gdt = DATA / "gdt_fixed.bin"
    fs = Ext4(reader, gdt_override=gdt.read_bytes() if gdt.exists() else None)

    def chunk_ready(abs_offset):
        if state is None:
            return True
        idx = abs_offset // chunk
        return idx < len(state) and state[idx] != ST_TODO

    marks = {}
    for r in files:
        try:
            raw, _csum_ok = fs.read_inode(r["ino"], passes=1)
            size = (struct.unpack_from("<I", raw, 0x04)[0] |
                    (struct.unpack_from("<I", raw, 0x6C)[0] << 32))
            if size == 0:
                marks[r["path"]] = MARK_READY
                continue

            ext = fs.extents(raw, passes=1) or []
            blocks = [phys + k for _lg, phys, cnt in ext for k in range(cnt)]
            if not blocks:
                marks[r["path"]] = MARK_UNKNOWN
                continue

            ok = sum(1 for b in blocks
                     if chunk_ready(reader.offset + b * fs.block_size))
            if ok == len(blocks):
                marks[r["path"]] = MARK_READY
            elif ok == 0:
                marks[r["path"]] = MARK_ABSENT
            else:
                marks[r["path"]] = MARK_PARTIAL
        except Exception:
            marks[r["path"]] = MARK_UNKNOWN

    return marks

import argparse

ap = argparse.ArgumentParser(description="перегляд дерева з tree.jsonl")
ap.add_argument("path", nargs="?", default="/", help="шлях, який показати")
ap.add_argument("--depth", type=int, default=2, help="глибина показу")
ap.add_argument("--find", help="пошук за частиною імені файлу")
ap.add_argument("--ready", action="store_true",
                help="показати готовність файлів в образі (читає inode, тому повільніше)")
ap.add_argument("--source", default=None, help="образ для перевірки готовності")
ap.add_argument("--max-check", type=int, default=3000,
                help="скільки файлів максимум перевіряти на готовність")
opts = ap.parse_args()

depth, find, root = opts.depth, opts.find, opts.path

records = [json.loads(l) for l in TREE.read_text().splitlines() if l.strip()]
print(f"дерево: {len(records)} записів з {TREE}\n")

if find:
    hits = [r for r in records if find.lower() in r["path"].rsplit("/", 1)[-1].lower()]
    print(f"знайдено {len(hits)} за '{find}' (перші 60):")
    for r in sorted(hits, key=lambda x: x["path"])[:60]:
        print(f"  [{KIND.get(r['k'], '?')}] {r['path']}")
    raise SystemExit(0)

prefix = root.rstrip("/") or ""
sub = [r for r in records if r["path"] == prefix or r["path"].startswith(prefix + "/")]
if not sub:
    print(f"нічого не знайдено під {root}")
    raise SystemExit(0)

marks = {}
if opts.ready:
    marks = build_readiness(sub, opts.source, 1048576, opts.max_check)
    print("готовність: '+' повністю в образі, '~' частково, '-' ще немає, ' ' невідомо\n")

base_depth = prefix.count("/")
print(f"=== {root} ===")
shown = 0
for r in sorted(sub, key=lambda x: x["path"]):
    level = r["path"].count("/") - base_depth
    if level < 1 or level > depth:
        continue
    indent = "  " * (level - 1)
    name = r["path"].rsplit("/", 1)[-1]
    mark = "/" if r["k"] == "d" else ""
    ready = marks.get(r["path"], MARK_UNKNOWN) if marks else ""
    prefix_mark = f"{ready} " if marks else ""
    print(f"  {prefix_mark}{indent}[{KIND.get(r['k'], '?')}] {name}{mark}")
    shown += 1
    if shown >= 400:
        print("  ... (обрізано на 400 рядках, уточни шлях або --depth)")
        break

# Скільки всього під цим шляхом - корисно, щоб бачити обсяг, не гортаючи
kinds = Counter(r["k"] for r in sub)
print(f"\nвсього під {root}: " + ", ".join(f"{KIND.get(k, k)}: {v}" for k, v in kinds.most_common()))

if marks:
    m = Counter(marks.values())
    total_checked = sum(m.values())
    print(f"готовність ({total_checked} перевірено): "
          f"повністю {m.get(MARK_READY, 0)}, частково {m.get(MARK_PARTIAL, 0)}, "
          f"ще немає {m.get(MARK_ABSENT, 0)}, невідомо {m.get(MARK_UNKNOWN, 0)}")
    if m.get(MARK_READY, 0) == total_checked and total_checked:
        print("  -> усе перевірене вже в образі: можна витягати (extract.py)")
