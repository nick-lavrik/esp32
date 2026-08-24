#!/usr/bin/env python3
"""Відновлення перекручених імен файлів за словником правильних імен.

Мерехтіння змінює в імені один-два БІТИ, а не символи: "blas.pc" стає
"blqs&pc", "firefox" - "&irefox". Тому правильне ім'я шукається не за
схожістю тексту, а за мінімальною БІТОВОЮ відстанню - і тільки серед імен
тієї ж довжини, бо кількість символів мерехтіння не змінює.

Словник кандидатів збирається з трьох джерел:
  1. локальна файлова система (та сама тека на цьому комп'ютері) - найкраще
     джерело для власних файлів користувача;
  2. надійні імена з самого образу (з блоків, чия csum сходиться) - ловить
     випадки, коли той самий файл є в кількох теках;
  3. сусідні записи в тій самій теці образу.

Скрипт НІЧОГО не перейменовує - лише друкує пропозиції з бітовою відстанню,
щоб рішення залишалось за людиною. Відстань 1 - майже напевно правильно,
3 і більше - вимагає перевірки очима.
"""
import os, sys, json, argparse
from collections import defaultdict
from pathlib import Path

DATA = Path(os.environ.get("SD_RESCUE_DATA",
                           Path(__file__).resolve().parent.parent / "data"))

ap = argparse.ArgumentParser()
ap.add_argument("path", nargs="?", default="/home/nick", help="тека в образі")
ap.add_argument("--local", default=None,
                help="відповідна тека на цьому комп'ютері (типово - той самий шлях)")
ap.add_argument("--max-bits", type=int, default=3, help="максимальна бітова відстань")
ap.add_argument("--limit", type=int, default=60, help="скільки пропозицій показати")
ap.add_argument("--min-len", type=int, default=4,
                help="мінімальна довжина імені: на коротких бітова відстань не інформативна")
ap.add_argument("--wide", action="store_true",
                help="шукати кандидатів по всьому образу (шумно; типово - "
                     "лише локальна ФС і та сама тека)")
opts = ap.parse_args()


# Символи, яких у нормальних іменах практично не буває. Наявність такого -
# ознака, що мерехтіння влучило саме в це ім'я, а не деінде в блоці.
BROKEN_MARKERS = set('\x00\ufffd`|&{}\\<>*?"\x01\x02\x03\x04\x05\x06\x07')


def looks_broken(name: str) -> bool:
    """Чи схоже ім'я на перекручене мерехтінням.

    НАВІЩО ЦЕЙ ФІЛЬТР: невалідна csum блоку каталогу означає, що побитий
    ЯКИЙСЬ байт у блоці - а блок містить сотні записів. Тому позначка
    "name_suspect" накриває всі імена блоку, хоча перекручене зазвичай одне.
    Без цього фільтра скрипт "виправляв" цілком правильні імена: пропонував
    etc -> Etc, ls -> ms, 55 -> 57 - тобто вносив помилки замість виправлення.
    """
    if any(c in BROKEN_MARKERS for c in name):
        return True
    if any(ord(c) < 32 or ord(c) == 127 for c in name):
        return True
    # Крапка в дивному місці або подвійні розширення на кшталт "pubringnkbx"
    # фільтр не ловить - такі випадки лишаються людині.
    return False


def bit_distance(a: str, b: str) -> int:
    """Скільки бітів різняться. 999, якщо довжина не збігається."""
    ab, bb = a.encode("utf-8", "surrogateescape"), b.encode("utf-8", "surrogateescape")
    if len(ab) != len(bb):
        return 999
    return sum(bin(x ^ y).count("1") for x, y in zip(ab, bb))


recs = [json.loads(l) for l in (DATA / "tree.jsonl").read_text().splitlines() if l.strip()]
prefix = opts.path.rstrip("/")
sub = [r for r in recs if r["path"] == prefix or r["path"].startswith(prefix + "/")]

# --- словники кандидатів --------------------------------------------------
# надійні імена з образу, згруповані за текою
trusted_by_dir = defaultdict(set)
for r in sub:
    if r.get("name_suspect"):
        continue
    parent, _, name = r["path"].rpartition("/")
    trusted_by_dir[parent].add(name)

# усі надійні імена образу - як загальний словник
trusted_all = set()
for r in recs:
    if not r.get("name_suspect"):
        trusted_all.add(r["path"].rpartition("/")[2])

local_root = Path(opts.local) if opts.local else Path(opts.path)
local_by_dir = {}


def local_names(image_dir: str) -> set:
    """Імена у відповідній теці на цьому комп'ютері."""
    if image_dir in local_by_dir:
        return local_by_dir[image_dir]
    rel = image_dir[len(prefix):].lstrip("/")
    cand = local_root / rel if rel else local_root
    try:
        names = {p.name for p in cand.iterdir()}
    except (OSError, PermissionError, ValueError):
        # ValueError - "embedded null byte": сам ШЛЯХ у теці образу може
        # містити перекручені байти (включно з нулем), і тоді відповідної
        # локальної теки просто не існує. Це нормальна ситуація, не помилка.
        names = set()
    local_by_dir[image_dir] = names
    return names


# Беремо не всі "підозрілі", а лише ті, що СПРАВДІ виглядають перекрученими.
all_suspects = [r for r in sub if r.get("name_suspect")]
suspects = [r for r in all_suspects
            if looks_broken(r["path"].rpartition("/")[2])
            and len(r["path"].rpartition("/")[2]) >= opts.min_len]
print(f"тека образу        : {prefix}")
print(f"локальний словник  : {local_root} {'(є)' if local_root.exists() else '(НЕМА)'}")
print(f"у блоках з побитою csum: {len(all_suspects)} імен")
print(f"з них СХОЖІ на перекручені: {len(suspects)} "
      f"(решта, найімовірніше, цілі - побитий байт був деінде в блоці)\n")

found = defaultdict(list)
stats = {"local": 0, "image_dir": 0, "image_all": 0, "none": 0}

for r in suspects:
    parent, _, name = r["path"].rpartition("/")

    best, best_d, src = None, opts.max_bits + 1, None
    sources = [(local_names(parent), "локальна ФС"),
               (trusted_by_dir.get(parent, set()), "образ: та сама тека")]
    if opts.wide:
        # Словник з усього образу дає кандидата майже завжди - і саме тому
        # він небезпечний: для короткого імені знайдеться сусід на один біт,
        # який не має до нього стосунку.
        sources.append((trusted_all, "образ: будь-де"))

    for cands, tag in sources:
        for c in cands:
            d = bit_distance(name, c)
            if d < best_d and d > 0:
                best, best_d, src = c, d, tag
        if best is not None and best_d <= 1:
            break        # відстань 1 - далі шукати нема сенсу

    if best is None:
        stats["none"] += 1
        continue

    key = {"локальна ФС": "local", "образ: та сама тека": "image_dir",
           "образ: будь-де": "image_all"}[src]
    stats[key] += 1
    found[best_d].append((r["path"], best, src))

print("=== результат ===")
print(f"  відновлено з локальної ФС      : {stats['local']}")
print(f"  відновлено з образу (та тека)  : {stats['image_dir']}")
print(f"  відновлено з образу (будь-де)  : {stats['image_all']}")
print(f"  кандидата не знайдено          : {stats['none']}")

shown = 0
for dist in sorted(found):
    print(f"\n--- бітова відстань {dist} ({len(found[dist])} імен) ---")
    for path, fixed, src in found[dist]:
        parent, _, name = path.rpartition("/")
        print(f"  {name:<36} -> {fixed:<36} [{src}]")
        shown += 1
        if shown >= opts.limit:
            print(f"  ... (обрізано на {opts.limit}; збільш --limit)")
            break
    if shown >= opts.limit:
        break
