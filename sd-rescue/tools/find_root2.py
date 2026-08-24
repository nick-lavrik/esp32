import os as _os_boot, sys as _sys_boot
_sys_boot.path.insert(0, _os_boot.path.dirname(_os_boot.path.abspath(__file__)))
#!/usr/bin/env python3
"""Пошук ВСІХ підкаталогів кореня, коли сам кореневий блок побитий.

Перша версія покладалась на послідовний ланцюжок записів (rec_len) і на
поле типу. На побитому блоці це втрачає записи: щойно rec_len мерехтить,
розбір обривається, а зіпсоване поле типу викидає валідний каталог.

Тому тут блок сканується по ВСІХ позиціях, кратних 4, і кандидатом
вважається будь-яка структура, що виглядає як запис каталогу. Остаточний
відбір - не за виглядом, а за фактом: читаємо inode кандидата і дивимось,
чи це каталог, чиє ".." дорівнює 2.
"""
import os, sys, struct, json
from collections import Counter
from ext4http import CardReader, Ext4, DEFAULT_SOURCE


def pick_source(argv):
    """Джерело даних: аргумент командного рядка, змінна SD_SOURCE або образ.

    Приймає і блоковий пристрій (/dev/sda - картка через USB), і файл образу,
    і URL HTTP-сервера на платі. Порядок такий, бо читати з образу зазвичай
    правильніше: не забирає пристрій у imager і показує рівно те, що вже
    знято.
    """
    for a in argv[1:]:
        if not a.startswith("-"):
            return a
    return os.environ.get("SD_SOURCE", DEFAULT_SOURCE)
from pathlib import Path

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


r = CardReader(pick_source(sys.argv))
fs = Ext4(r, gdt_override=(SP / "gdt_fixed.bin").read_bytes())
root_raw, _ = fs.read_inode(2, passes=1)
phys = fs.extents(root_raw)[0][1]

N = 30
reads = [r.read_raw(phys * fs.block_size, fs.block_size) for _ in range(N)]

# Кандидати: (ім'я, inode) -> скільки разів трапилось.
cands = Counter()
for data in reads:
    for pos in range(0, fs.block_size - 12, 4):
        ino, rl, nl, ft = struct.unpack_from("<IHBB", data, pos)
        if not (0 < ino <= fs.inodes_count):
            continue
        if not (1 <= nl <= 255) or rl < 8 + nl or rl % 4 or pos + rl > fs.block_size:
            continue
        name = data[pos+8:pos+8+nl]
        if not all(32 < c < 127 and c not in (0x2F,) for c in name):
            continue
        cands[(name.decode(), ino)] += 1

print("кандидатів: %d" % len(cands))

# Групуємо за inode: одне й те саме inode часто дає кілька варіантів імені.
by_ino = {}
for (name, ino), cnt in cands.items():
    by_ino.setdefault(ino, Counter())[name] += cnt

verified = {}
print("\n%-10s %-18s %s" % ("inode", "варіанти імені", "перевірка"))
print("-" * 70)
for ino, names in sorted(by_ino.items(), key=lambda x: -max(x[1].values())):
    if max(names.values()) < 2:
        continue
    try:
        raw, _ok = fs.read_inode(ino, passes=1)
        mode = struct.unpack_from("<H", raw, 0x00)[0]
        if (mode & 0xF000) != 0x4000:
            continue
        ext = fs.extents(raw)
        if not ext:
            continue
        d = r.read_raw(ext[0][1] * fs.block_size, fs.block_size)
        first_rl = struct.unpack_from("<IHBB", d, 0)[1]
        parent = struct.unpack_from("<I", d, first_rl)[0]
    except Exception as e:
        continue
    if parent != 2:
        continue
    best = names.most_common(1)[0][0]
    verified[best] = ino
    print("%-10d %-18s ПІДТВЕРДЖЕНО (варіанти: %s)" % (
        ino, best, ", ".join("%s:%d" % (n, c) for n, c in names.most_common(4))))

print("-" * 70)
print("підтверджених підкаталогів кореня: %d" % len(verified))
(SP / "root_children2.json").write_text(json.dumps(verified, indent=1))
