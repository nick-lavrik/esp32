import os as _os_boot, sys as _sys_boot
_sys_boot.path.insert(0, _os_boot.path.dirname(_os_boot.path.abspath(__file__)))
#!/usr/bin/env python3
"""Обхід дерева каталогів ext4 напряму з картки: повний перелік файлів.

Працює в обхід ядра, бо ядро відмовляється монтувати ФС (25% дескрипторів
груп незворотно побито в хвості картки). Кожен блок каталогу перевіряється
власною контрольною сумою; якщо вона не сходиться - блок перечитується,
бо частина клітинок картки мерехтить.

Імена в корені відновлюються окремо: його блок деградував найсильніше, але
всі імена там стандартні для Linux, а мерехтіння змінює один-два біти, тож
кандидат однозначно визначається найближчим стандартним ім'ям.
"""
import os, sys, struct, json, time
from ext4http import CardReader, Ext4, crc32c, DEFAULT_SOURCE


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


# Пишемо в тимчасовий файл і перейменовуємо лише після успіху.
#
# НАВІЩО: попередня версія відкривала tree.jsonl на запис ще на старті, тому
# будь-яке падіння (у моєму випадку - побитий root_children2.json) знищувало
# результат попереднього успішного обходу. Дерево на 140 тисяч записів
# коштувало годин читання з деградованої картки, і втрачати його через
# невдалий перезапуск неприпустимо.
OUT_FINAL = SP / "tree.jsonl"
OUT_TMP = SP / "tree.jsonl.partial"
OUT = OUT_TMP.open("w")
LOG = SP / "walk_progress.txt"

STD_ROOT = ["bin","boot","dev","etc","home","lib","lib32","lib64","libx32",
            "lost+found","media","mnt","opt","proc","root","run","sbin","srv",
            "sys","tmp","usr","var"]

def bit_distance(a, b):
    if len(a) != len(b):
        return 999
    return sum(bin(x ^ y).count("1") for x, y in zip(a.encode(), b.encode()))

def fix_root_name(name):
    """Найближче стандартне ім'я, якщо відрізняється кількома бітами."""
    if name in STD_ROOT:
        return name, False
    best, dist = None, 999
    for cand in STD_ROOT:
        d = bit_distance(name, cand)
        if d < dist:
            best, dist = cand, d
    return (best, True) if dist <= 3 else (name, False)

def log(msg):
    with LOG.open("a") as f:
        f.write("%s %s\n" % (time.strftime("%H:%M:%S"), msg))

r = CardReader(pick_source(sys.argv))
fs = Ext4(r, gdt_override=(SP / "gdt_fixed.bin").read_bytes())

def dir_entries(ino, passes=5):
    """Записи каталогу з перевіркою контрольної суми кожного блоку."""
    raw, inode_ok = fs.read_inode(ino, passes=3)
    mode = struct.unpack_from("<H", raw, 0x00)[0]
    if (mode & 0xF000) != 0x4000:
        return None, inode_ok, 0, 0, 0
    seed = fs.inode_csum_seed(ino, raw)
    size = struct.unpack_from("<I", raw, 0x04)[0]

    entries, blocks_ok, blocks_bad, blocks_absent = [], 0, 0, 0
    seen = 0
    for _logical, phys in fs.read_file_blocks(raw, passes=3):
        if seen >= size:
            break
        seen += fs.block_size

        data = None
        for attempt in range(passes):
            cand = r.read_raw(phys * fs.block_size, fs.block_size)

            # Суцільні нулі - це блок, якого ЩЕ НЕМА в образі (дірка
            # sparse-файлу), а не побитий блок. Раніше такі випадки не
            # потрапляли ні в "ok", ні в "побиті", і статистика змішувала
            # "прочитано й побито" з "ще не прочитано" - за нею неможливо
            # було зрозуміти, чи це втрата даних, чи просто незнята частина.
            if not any(cand):
                blocks_absent += 1
                data = cand
                break

            tail = cand[-12:]
            z1, rl, _z2, ft = struct.unpack_from("<IHBB", tail, 0)
            if not (z1 == 0 and rl == 12 and ft == 0xDE):
                data = cand          # хвоста немає - перевіряти нічим
                break
            if crc32c(seed, cand[:-12]) == struct.unpack_from("<I", tail, 8)[0]:
                data = cand
                blocks_ok += 1
                break
            data = cand
        else:
            blocks_bad += 1

        # Чи можна довіряти іменам із цього блоку. Запис несе цю ознаку далі,
        # у tree.jsonl: без неї неможливо відрізнити правильне ім'я від
        # перекрученого мерехтінням ("blqs&pc" замість "blas.pc"), а таких
        # блоків серед прочитаних майже половина.
        block_trusted = (data is not None) and any(data) and (
            crc32c(seed, data[:-12]) == struct.unpack_from("<I", data[-12:], 8)[0]
            if (struct.unpack_from("<IHBB", data[-12:], 0)[1] == 12 and
                struct.unpack_from("<IHBB", data[-12:], 0)[3] == 0xDE)
            else False)

        pos = 0
        while pos < fs.block_size - 8:
            e_ino, rl, nl, ft = struct.unpack_from("<IHBB", data, pos)
            if rl < 8 or rl % 4 or pos + rl > fs.block_size:
                break
            if e_ino and nl and nl <= rl - 8:
                nm = data[pos+8:pos+8+nl]
                if nm not in (b".", b".."):
                    entries.append((e_ino, ft, nm.decode("utf-8", "replace"), block_trusted))
            pos += rl
    return entries, inode_ok, blocks_ok, blocks_bad, blocks_absent

# --- корінь ---------------------------------------------------------------
# Список підкаталогів кореня: результат find_root2.py. Якщо його немає або
# він побитий - кажемо це прямо, разом з командою, що його відтворює.
_rc = SP / "root_children2.json"
try:
    root_children = json.loads(_rc.read_text())
    if not root_children:
        raise ValueError("файл порожній")
except (FileNotFoundError, json.JSONDecodeError, ValueError) as e:
    OUT.close()
    OUT_TMP.unlink(missing_ok=True)
    raise SystemExit(
        f"не читається {_rc} ({e}).\n"
        f"Відтвори його: python3 {Path(__file__).parent}/find_root2.py <джерело>")
KIND = {1: "f", 2: "d", 7: "l", 10: "?"}

queue = []
for name, ino in root_children.items():
    if name == "..":
        continue
    fixed, was_fixed = fix_root_name(name)
    queue.append(("/" + fixed, ino, was_fixed))
    OUT.write(json.dumps({"path": "/" + fixed, "ino": ino, "k": "d",
                          "name_repaired": was_fixed}) + "\n")

# Корінь містить і те, чого голосування не дало (usr, var, tmp, sys...).
# Їх знаходимо за загальним правилом: підкаталог кореня має ".." == 2.
log("старт обходу, підкаталогів кореня з голосування: %d" % len(queue))

visited = set()
stats = {"dirs": 0, "files": 0, "links": 0, "other": 0,
         "dir_blocks_ok": 0, "dir_blocks_bad": 0, "dir_blocks_absent": 0,
         "inode_bad": 0}
start = time.time()

while queue:
    path, ino, _rep = queue.pop(0)
    if ino in visited:
        continue
    visited.add(ino)

    try:
        entries, inode_ok, ok, bad, absent = dir_entries(ino)
    except Exception as e:
        log("каталог %s (inode %d): %s" % (path, ino, e))
        continue

    if entries is None:
        continue
    if not inode_ok:
        stats["inode_bad"] += 1
    stats["dirs"] += 1
    stats["dir_blocks_ok"] += ok
    stats["dir_blocks_bad"] += bad
    stats["dir_blocks_absent"] += absent

    for e_ino, ft, name, name_trusted in entries:
        if e_ino > fs.inodes_count:
            continue
        child = path.rstrip("/") + "/" + name
        kind = KIND.get(ft, "?")
        rec = {"path": child, "ino": e_ino, "k": kind}
        if not name_trusted:
            rec["name_suspect"] = 1
        if kind == "d":
            stats["dirs"] += 0
            queue.append((child, e_ino, False))
        elif kind == "f":
            stats["files"] += 1
        elif kind == "l":
            stats["links"] += 1
        else:
            stats["other"] += 1
        OUT.write(json.dumps(rec) + "\n")

    if stats["dirs"] % 100 == 0:
        OUT.flush()
        log("каталогів %d, файлів %d, черга %d | блоки каталогів ok %d / bad %d | %.1f MiB, %.0fs"
            % (stats["dirs"], stats["files"], len(queue), stats["dir_blocks_ok"],
               stats["dir_blocks_bad"], r.bytes_read/1048576, time.time()-start))

OUT.close()
OUT_TMP.replace(OUT_FINAL)   # атомарна заміна: старий результат живий до кінця
log("ГОТОВО %s за %.0fs, %.1f MiB" % (json.dumps(stats), time.time()-start, r.bytes_read/1048576))

# Підсумок на екран. Раніше скрипт писав лише у файл і мовчав, через що
# успішний обхід на 123 тисячі записів виглядав так, ніби нічого не сталося.
print("=" * 70)
print("ОБХІД ЗАВЕРШЕНО за %.0f с, прочитано %.1f MiB" % (time.time() - start, r.bytes_read / 1048576))
print("=" * 70)
print("  каталогів     : %d" % stats["dirs"])
print("  файлів        : %d" % stats["files"])
print("  symlink       : %d" % stats["links"])
print("  інших         : %d" % stats["other"])
print()
print("  блоки каталогів:")
print("    цілі (csum сходиться)   : %d" % stats["dir_blocks_ok"])
print("    ПОБИТІ (прочитані, csum не сходиться): %d" % stats["dir_blocks_bad"])
print("    ще не в образі (нулі)   : %d" % stats["dir_blocks_absent"])
_read = stats["dir_blocks_ok"] + stats["dir_blocks_bad"]
if _read:
    print("    -> серед ПРОЧИТАНИХ побито %.0f%%" % (100.0 * stats["dir_blocks_bad"] / _read))
print("  inode з невірною csum: %d" % stats["inode_bad"])
print()
print("  результат: %s (%d записів)" % (SP / "tree.jsonl", stats["dirs"] + stats["files"] + stats["links"] + stats["other"]))
print()
print("Подивитись дерево:")
print("  python3 %s/showtree.py            # верхні рівні" % Path(__file__).parent)
print("  python3 %s/showtree.py /home/nick # конкретна тека" % Path(__file__).parent)
