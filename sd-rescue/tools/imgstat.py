#!/usr/bin/env python3
"""Що вже є в образі — не перериваючи знімання.

Читати образ під час роботи imager безпечно: той лише пише, а карта станів
(.state) оновлюється кожні кілька шматків, тому картина завжди актуальна з
точністю до секунд.

Показує три речі:
  1. скільки шматків знято, пропущено як порожні, зіпсовано;
  2. чи вже на місці структури, без яких образ не змонтується (суперблок,
     дескриптори груп, inode-таблиця кореня);
  3. чи можна вже спробувати прочитати дерево каталогів.
"""
import os, sys, json, struct
from pathlib import Path

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
DATA = Path(os.environ.get("SD_RESCUE_DATA",
                           Path(__file__).resolve().parent.parent / "data"))

ST_TODO, ST_OK, ST_FLAKY, ST_ERROR, ST_SKIP = 0, 1, 2, 3, 4
NAMES = {ST_TODO: "ще не знято", ST_OK: "знято надійно", ST_FLAKY: "мерехтіло",
         ST_ERROR: "помилка читання", ST_SKIP: "порожньо (пропущено)"}

img = Path(sys.argv[1] if len(sys.argv) > 1 else Path.home() / "sd-rescue/card.img")
state_path = img.with_suffix(img.suffix + ".state")

if not img.exists():
    raise SystemExit(f"образу немає: {img}")

apparent = img.stat().st_size
real = img.stat().st_blocks * 512

print(f"образ      : {img}")
print(f"розмір     : {apparent/1024**3:.1f} GiB заявлено, {real/1024**3:.2f} GiB реально на диску")

if state_path.exists():
    state = state_path.read_bytes()
    counts = {}
    for b in state:
        counts[b] = counts.get(b, 0) + 1
    total = len(state)
    print(f"шматків    : {total} по 1 MiB")
    for code in (ST_OK, ST_FLAKY, ST_ERROR, ST_SKIP, ST_TODO):
        n = counts.get(code, 0)
        if n:
            print(f"  {NAMES[code]:24s}: {n:>7} ({100.0*n/total:5.1f}%)")
    done = total - counts.get(ST_TODO, 0)
    print(f"пройдено   : {100.0*done/total:.2f}% карти")

# --- чи на місці структури, потрібні для монтування ----------------------
meta_path = DATA / "occupied_meta.json"
if not meta_path.exists():
    print("\n(немає occupied_meta.json — перевірку структур пропускаю)")
    raise SystemExit(0)

meta = json.loads(meta_path.read_text())
part_off = meta["part_offset"]
bs = meta["block_size"]

def read_at(offset, length):
    with img.open("rb") as f:
        f.seek(offset)
        return f.read(length)

def is_hole(data):
    return data.count(0) == len(data)

print("\n=== структури ext4 в образі ===")
sb = read_at(part_off + 1024, 1024)
magic = struct.unpack_from("<H", sb, 0x38)[0] if len(sb) >= 0x3A else 0
sb_ok = magic == 0xEF53
print(f"суперблок          : {'НА МІСЦІ (magic 0xEF53)' if sb_ok else 'ще немає'}")

if sb_ok:
    label = sb[0x78:0x88].split(b"\x00")[0].decode("utf-8", "replace")
    blocks = struct.unpack_from("<I", sb, 0x04)[0]
    free = struct.unpack_from("<I", sb, 0x0C)[0]
    print(f"  label            : {label or '(порожня)'}")
    print(f"  блоків           : {blocks} всього / {free} вільних")
    print(f"  зайнято даними   : {(blocks-free)*bs/1024**3:.2f} GiB")

gdt = read_at(part_off + bs, 64 * 1024)
print(f"дескриптори груп   : {'є дані' if not is_hole(gdt) else 'ще немає'}")

# inode-таблиця групи 0 - без неї не прочитати навіть корінь
if sb_ok:
    gdt_full = read_at(part_off + bs, 32)
    itable = struct.unpack_from("<I", gdt_full, 0x08)[0] if len(gdt_full) >= 12 else 0
    if itable and itable < meta["blocks"]:
        chunk = read_at(part_off + itable * bs, bs)
        print(f"inode-таблиця гр.0 : {'є дані' if not is_hole(chunk) else 'ще немає'} (блок {itable})")

# --- готовність метаданих: скільки inode-таблиць уже в образі -------------
#
# Це прямий індикатор того, чи є сенс запускати обхід дерева. Каталоги й
# розміри файлів живуть в inode-таблицях, по одній на групу блоків; поки їх
# немає, walk_tree знайде лише кілька каталогів і зупиниться.
gdt_bytes = (DATA / "gdt_fixed.bin").read_bytes() if (DATA / "gdt_fixed.bin").exists() else b""
desc_size = 32
if gdt_bytes and sb_ok:
    groups = meta["groups"]
    checked = ready = 0
    # Перевіряємо кожну 20-ту групу: повний прохід - це 1860 читань по 4 KiB,
    # і на образі, який зараз пишеться, вони змагаються за диск із imager.
    for g in range(0, groups, 20):
        off = g * desc_size
        if off + 12 > len(gdt_bytes):
            break
        itable = struct.unpack_from("<I", gdt_bytes, off + 0x08)[0]
        if itable == 0 or itable >= meta["blocks"]:
            continue
        checked += 1
        if not is_hole(read_at(part_off + itable * bs, 4096)):
            ready += 1

    if checked:
        pct = 100.0 * ready / checked
        print(f"\n=== готовність метаданих ===")
        print(f"inode-таблиці      : {ready} з {checked} перевірених груп ({pct:.0f}%)")
        if pct < 5:
            print("  обхід дерева поки нічого не покаже - метаданих майже немає")
        elif pct < 60:
            print("  обхід дерева вже щось покаже, але буде неповним")
        else:
            print("  метаданих достатньо: обхід дерева має дати повну картину")

print("\nдерево каталогів можна прочитати з ОБРАЗУ (не займає картку):")
print(f"  python3 {Path(__file__).parent}/walk_tree.py {img}")
print("а спробувати змонтувати (лише коли карта пройдена повністю):")
print(f"  sudo losetup -rP /dev/loop9 {img} && sudo mount -o ro,noload /dev/loop9p2 /mnt/sdcard")
