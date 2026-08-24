#!/usr/bin/env python3
"""Прочитати конкретний файл з образу, який ще наповнюється.

Використання:
    catfile.py /etc/hostname                 # вивести на екран
    catfile.py /etc/fstab --out ./fstab      # зберегти
    catfile.py /home/nick/Work/x.jar --out ~/x.jar
    catfile.py /etc/passwd --check           # лише перевірити готовність

ГОЛОВНЕ, ЩО ТУТ РОБИТЬСЯ ПРАВИЛЬНО: перед читанням перевіряється, чи блоки
файлу ВЖЕ скопійовані в образ. Наївна перевірка "чи там нулі" тут не
годиться - файл може законно містити нулі, і тоді порожні ділянки образу
неможливо відрізнити від справжніх даних. Тому готовність беремо з карти
станів imager (.state): вона знає, які шматки образу вже пройдені.

Другий шар перевірки - контрольна сума inode. Якщо вона не сходиться, розмір
і список блоків файлу могли бути прочитані з мерехтливими бітами, і про це
попереджається окремо: саме так один .gitignore з 26 байтів перетворився на
524346 байтів сміття.
"""
import os, sys, json, struct, argparse
from pathlib import Path

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ext4http import CardReader, Ext4, DEFAULT_SOURCE

DATA = Path(os.environ.get("SD_RESCUE_DATA",
                           Path(__file__).resolve().parent.parent / "data"))
ST_TODO = 0

ap = argparse.ArgumentParser()
ap.add_argument("path", help="шлях файлу у файловій системі картки")
ap.add_argument("--source", default=DEFAULT_SOURCE, help="образ або /dev/sdX")
ap.add_argument("--out", help="куди зберегти (без цього - вивід на екран)")
ap.add_argument("--check", action="store_true", help="лише перевірити готовність")
ap.add_argument("--verify", action="store_true",
                help="перечитати блоки файлу з КАРТКИ кілька разів і сказати, "
                     "чи дані повторюються (лише з --source /dev/sdX)")
ap.add_argument("--passes", type=int, default=3, help="скільки читань для --verify")
ap.add_argument("--chunk", type=int, default=1048576, help="розмір шматка карти станів")
opts = ap.parse_args()

# --- де файл у дереві -----------------------------------------------------
tree = DATA / "tree.jsonl"
if not tree.exists():
    raise SystemExit(f"немає {tree} - спершу walk_tree.py")

rec = None
for line in tree.read_text().splitlines():
    if not line.strip():
        continue
    r = json.loads(line)
    if r["path"] == opts.path:
        rec = r
        break

if rec is None:
    raise SystemExit(f"у дереві немає {opts.path} (перевір showtree.py --find)")

# --- карта станів образу: які шматки вже знято ----------------------------
state = None
state_path = Path(opts.source + ".state")
if state_path.exists():
    state = state_path.read_bytes()

def chunk_ready(byte_offset):
    """Чи скопійований шматок образу, у який попадає цей байт."""
    if state is None:
        return True          # карти немає (читаємо з картки) - все доступне
    idx = byte_offset // opts.chunk
    return idx < len(state) and state[idx] != ST_TODO

# --- читаємо inode і extent'и --------------------------------------------
r = CardReader(opts.source)
fs = Ext4(r, gdt_override=(DATA / "gdt_fixed.bin").read_bytes()
          if (DATA / "gdt_fixed.bin").exists() else None)

raw, inode_csum_ok = fs.read_inode(rec["ino"], passes=5)
mode = struct.unpack_from("<H", raw, 0x00)[0]
size = struct.unpack_from("<I", raw, 0x04)[0] | (struct.unpack_from("<I", raw, 0x6C)[0] << 32)

print(f"файл    : {opts.path}", file=sys.stderr)
print(f"inode   : {rec['ino']}, csum {'OK' if inode_csum_ok else 'НЕ ЗБІГАЄТЬСЯ'}", file=sys.stderr)
print(f"розмір  : {size} байт{' (НЕ ДОВІРЯТИ - csum inode невірна)' if not inode_csum_ok else ''}",
      file=sys.stderr)

if (mode & 0xF000) == 0xA000:
    target = raw[0x28:0x28 + 60].split(b"\x00")[0].decode("utf-8", "replace")
    print(f"symlink -> {target}", file=sys.stderr)
    raise SystemExit(0)

if (mode & 0xF000) != 0x8000:
    raise SystemExit(f"не звичайний файл (mode {mode:#o})")

ext = fs.extents(raw, passes=5) or []
mapping = {}
for logical, phys, count in ext:
    for k in range(count):
        mapping[logical + k] = phys + k

total_blocks = (size + fs.block_size - 1) // fs.block_size
ready = missing = holes = 0
for lb in range(total_blocks):
    if lb not in mapping:
        holes += 1
        continue
    byte_off = fs.offset + mapping[lb] * fs.block_size if hasattr(fs, "offset") else None
    abs_off = r.offset + mapping[lb] * fs.block_size
    if chunk_ready(abs_off):
        ready += 1
    else:
        missing += 1

print(f"блоків  : {total_blocks} всього / {ready} готових в образі / "
      f"{missing} ще не знято / {holes} дірок у файлі", file=sys.stderr)

# --- перевірка САМИХ ДАНИХ на повторюваність -----------------------------
#
# Це єдиний доступний спосіб дізнатися, чи можна вірити вмісту файлу: у даних
# ext4 контрольних сум немає, тому пошкодження не видно "зсередини". Валідна
# csum inode гарантує лише правильність метаданих - розміру й списку блоків.
#
# Саме через цю різницю /etc/auto.sshfs мав статус "усе добре", хоча всередині
# лежав текст із перевернутими бітами ("/iome/nick", "it_rca").
if opts.verify:
    if str(opts.source).endswith(".img"):
        raise SystemExit("--verify має сенс лише з живою карткою: --source /dev/sdX\n"
                         "(образ - це вже результат одного читання, повторне читання "
                         "з нього завжди дасть те саме)")

    flaky_blocks = []
    for lb in range(total_blocks):
        if lb not in mapping:
            continue
        pos = mapping[lb] * fs.block_size
        variants = {r.read_raw(pos, fs.block_size) for _ in range(opts.passes)}
        if len(variants) > 1:
            flaky_blocks.append(lb)

    if flaky_blocks:
        print(f"вердикт : ДАНІ МЕРЕХТЯТЬ - {len(flaky_blocks)} з {total_blocks} блоків "
              f"читаються щоразу інакше", file=sys.stderr)
        print(f"          вмісту цього файлу довіряти НЕ можна", file=sys.stderr)
        raise SystemExit(3)

    print(f"вердикт : дані стабільні ({opts.passes} читань дали однаковий результат)",
          file=sys.stderr)
    raise SystemExit(0)

if opts.check:
    verdict = ("ГОТОВИЙ повністю" if missing == 0 else
               f"НЕПОВНИЙ - бракує {missing} з {total_blocks} блоків")
    print(f"вердикт : {verdict}", file=sys.stderr)
    raise SystemExit(0 if missing == 0 else 2)

if missing and not opts.out:
    print(f"\nУВАГА: {missing} блоків ще не знято - вони будуть нулями\n", file=sys.stderr)

# --- читаємо вміст --------------------------------------------------------
out = open(opts.out, "wb") if opts.out else sys.stdout.buffer
written = 0
for lb in range(total_blocks):
    take = min(fs.block_size, size - written)
    if lb not in mapping:
        out.write(b"\x00" * take)
    else:
        abs_off = r.offset + mapping[lb] * fs.block_size
        if chunk_ready(abs_off):
            data = r.read_raw(mapping[lb] * fs.block_size, fs.block_size)
            out.write(data[:take])
        else:
            out.write(b"\x00" * take)
    written += take

if opts.out:
    out.close()
    print(f"\nзбережено: {opts.out} ({written} байт)", file=sys.stderr)
