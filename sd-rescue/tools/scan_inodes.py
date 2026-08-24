import os as _os_boot, sys as _sys_boot
_sys_boot.path.insert(0, _os_boot.path.dirname(_os_boot.path.abspath(__file__)))
#!/usr/bin/env python3
"""Сканування inode-таблиць ext4 напряму з картки через HTTP.

НАВІЩО В ОБХІД КАТАЛОГІВ: блоки каталогів на цій картці мерехтять так
сильно (до 250 мерехтливих байтів на 4 KiB), що відновити їх точно
неможливо. А inode-таблиці збереглися майже цілими - вибіркова перевірка
дала 94-100% валідних контрольних сум. Кожен inode містить тип, розмір,
часи і extent-дерево, тобто ПОВНИЙ перелік вмісту з розташуванням даних.
Імена файлів живуть у каталогах і додаються окремо, де вдається.

Читаємо лише задіяну частину кожної таблиці: bg_itable_unused каже,
скільки inode у хвості ніколи не використовувались - на 232-гігабайтній
ФС це скорочує читання з 3.7 GiB до ~150 MiB.
"""
import os, sys, struct, json, time
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


OUT = SP / "inodes.jsonl"
LOG = SP / "scan_progress.txt"
CHUNK_BLOCKS = 64          # 256 KiB за один HTTP-запит

r = CardReader(pick_source(sys.argv))
fs = Ext4(r, gdt_override=(SP / "gdt_fixed.bin").read_bytes())

inodes_per_block = fs.block_size // fs.inode_size
table_blocks = (fs.inodes_per_group * fs.inode_size) // fs.block_size

out = OUT.open("w")
stats = {"groups_done": 0, "groups_skipped": 0, "inodes_used": 0,
         "csum_ok": 0, "csum_bad": 0, "dirs": 0, "files": 0, "links": 0}
start = time.time()

def log(msg):
    with LOG.open("a") as f:
        f.write(f"{time.strftime('%H:%M:%S')} {msg}\n")

log(f"старт: {fs.groups} груп, inode/група {fs.inodes_per_group}, "
    f"inode_size {fs.inode_size}, table_blocks {table_blocks}")

for group in range(fs.groups):
    d = fs.group_desc(group)
    itable = fs.inode_table_block(group)
    free_inodes = struct.unpack_from("<H", d, 0x0E)[0]
    itable_unused = struct.unpack_from("<H", d, 0x1C)[0]

    # Дескриптор побитий -> адреса таблиці недостовірна, групу пропускаємо.
    if itable == 0 or itable >= fs.blocks:
        stats["groups_skipped"] += 1
        continue

    used_head = fs.inodes_per_group - itable_unused
    if not (0 < used_head <= fs.inodes_per_group):
        used_head = fs.inodes_per_group      # значення не варте довіри - читаємо все

    blocks_to_read = min(table_blocks, (used_head + inodes_per_block - 1) // inodes_per_block)

    blk = 0
    while blk < blocks_to_read:
        take = min(CHUNK_BLOCKS, blocks_to_read - blk)
        try:
            data = r.read_raw((itable + blk) * fs.block_size, take * fs.block_size)
        except Exception as e:
            log(f"група {group}, блок {itable+blk}: {e}")
            blk += take
            continue

        for k in range(take * inodes_per_block):
            ino = group * fs.inodes_per_group + (blk * inodes_per_block) + k + 1
            off = k * fs.inode_size
            raw = data[off:off + fs.inode_size]

            mode = struct.unpack_from("<H", raw, 0x00)[0]
            links = struct.unpack_from("<H", raw, 0x1A)[0]
            dtime = struct.unpack_from("<I", raw, 0x14)[0]

            if mode == 0 and links == 0:
                continue

            stats["inodes_used"] += 1
            csum_ok = fs._inode_csum_ok(ino, raw)
            stats["csum_ok" if csum_ok else "csum_bad"] += 1

            kind = {0x8000: "f", 0x4000: "d", 0xA000: "l"}.get(mode & 0xF000, "?")
            if kind == "d": stats["dirs"] += 1
            elif kind == "f": stats["files"] += 1
            elif kind == "l": stats["links"] += 1

            rec = {
                "ino": ino,
                "k": kind,
                "mode": mode & 0xFFF,
                "uid": struct.unpack_from("<H", raw, 0x02)[0],
                "gid": struct.unpack_from("<H", raw, 0x18)[0],
                "size": struct.unpack_from("<I", raw, 0x04)[0] |
                        (struct.unpack_from("<I", raw, 0x6C)[0] << 32),
                "links": links,
                "mtime": struct.unpack_from("<I", raw, 0x10)[0],
                "csum": 1 if csum_ok else 0,
                "deleted": 1 if dtime else 0,
                "iblock": raw[0x28:0x28+60].hex(),
                "flags": struct.unpack_from("<I", raw, 0x20)[0],
            }
            out.write(json.dumps(rec) + "\n")

        blk += take

    stats["groups_done"] += 1
    if group % 25 == 0:
        out.flush()
        el = time.time() - start
        log(f"група {group}/{fs.groups} | inode {stats['inodes_used']} "
            f"(csum ok {stats['csum_ok']}, bad {stats['csum_bad']}) | "
            f"файлів {stats['files']}, каталогів {stats['dirs']} | "
            f"{r.bytes_read/1048576:.1f} MiB за {el:.0f}s")

out.close()
log(f"ГОТОВО: {json.dumps(stats)}  за {time.time()-start:.0f}s, "
    f"{r.bytes_read/1048576:.1f} MiB, запитів {r.http_reads}")
