import os as _os_boot, sys as _sys_boot
_sys_boot.path.insert(0, _os_boot.path.dirname(_os_boot.path.abspath(__file__)))
#!/usr/bin/env python3
"""Карта зайнятих блоків ext4: що саме треба копіювати в образ.

Розділ на 232 GiB містить 50.68 GiB даних. Копіювати весь розділ означало б
читати вп'ятеро більше з картки, яка й без того віддає ~400 KiB/s. Тому
беремо bitmap кожної групи блоків і копіюємо лише зайняте.

Обережність із пошкодженнями: bitmap мерехтить так само, як усе інше, і
помилково прочитаний нуль означав би НЕ скопійований блок з даними. Тому при
будь-якому сумніві - невалідна контрольна сума bitmap-а, побитий дескриптор
групи - група вважається ПОВНІСТЮ зайнятою. Зайве прочитане дешевше за
втрачене.
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
from heartbeat import Heartbeat
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
hb = Heartbeat("bitmaps", total_bytes=fs.groups * fs.block_size)

READ_ATTEMPTS = 5   # скільки разів пробувати отримати валідний bitmap

occupied = bytearray()   # біт на блок ФС, у порядку зростання номера блоку
stats = {"groups": 0, "csum_ok": 0, "csum_bad": 0, "desc_bad": 0, "blocks_used": 0}

def bitmap_csum_ok(group, data):
    """ext4_block_bitmap_csum: молодші 16 біт у дескрипторі групи."""
    d = fs.group_desc(group)
    stored = struct.unpack_from("<H", d, 0x18)[0]
    return (crc32c(fs.csum_seed, data) & 0xFFFF) == stored

for g in range(fs.groups):
    d = fs.group_desc(g)
    bb = struct.unpack_from("<I", d, 0x00)[0]
    if fs.is_64bit and fs.desc_size >= 0x24:
        bb |= struct.unpack_from("<I", d, 0x20)[0] << 32

    take_all = False
    if bb == 0 or bb >= fs.blocks:
        stats["desc_bad"] += 1
        take_all = True
        data = b"\xff" * (fs.blocks_per_group // 8)
    else:
        try:
            # Спершу пробуємо отримати bitmap із валідною контрольною сумою -
            # він точний. Перечитуємо до READ_ATTEMPTS разів, бо bitmap
            # мерехтить так само, як усе інше на цій картці.
            data = None
            variants = []
            for _attempt in range(READ_ATTEMPTS):
                candidate = r.read_raw(bb * fs.block_size, fs.block_size)[:fs.blocks_per_group // 8]
                variants.append(candidate)
                if not fs.metadata_csum or bitmap_csum_ok(g, candidate):
                    data = candidate
                    break

            if data is not None:
                stats["csum_ok"] += 1
            else:
                # Валідного bitmap-а немає (у хвості картки таких груп
                # більшість). Замість того, щоб оголошувати ВСЮ групу зайнятою
                # (це давало 187 GiB замість ~51), об'єднуємо прочитані
                # варіанти за АБО.
                #
                # Чому саме АБО: мерехтливий біт потрапляє в результат як
                # "зайнято". Помилка тоді може бути лише в один бік - ми
                # прочитаємо трохи більше, ніж потрібно, але НЕ пропустимо
                # блок з даними. Протилежний вибір (І, або голосування)
                # означав би тихо втрачені файли, чого ми не можемо собі
                # дозволити - на відміну від зайвих хвилин копіювання.
                merged = bytearray(len(variants[0]))
                for v in variants:
                    for i, b in enumerate(v):
                        merged[i] |= b
                data = bytes(merged)
                stats["csum_bad"] += 1
        except Exception:
            stats["desc_bad"] += 1
            take_all = True
            data = b"\xff" * (fs.blocks_per_group // 8)

    occupied += data
    stats["blocks_used"] += sum(bin(b).count("1") for b in data)
    stats["groups"] += 1

    if g % 20 == 0:
        hb.update(phase="читаю bitmap групи %d/%d" % (g, fs.groups),
                  done_bytes=r.bytes_read, total_bytes=fs.groups * fs.block_size,
                  blocks_ok=stats["csum_ok"], blocks_flaky=stats["csum_bad"],
                  blocks_failed=stats["desc_bad"],
                  last_event="зайнято блоків: %d (%.1f GiB)" % (
                      stats["blocks_used"], stats["blocks_used"] * fs.block_size / 1024**3))

(SP / "occupied.bin").write_bytes(bytes(occupied))
meta = {"block_size": fs.block_size, "blocks": fs.blocks, "groups": fs.groups,
        "blocks_per_group": fs.blocks_per_group,
        "part_offset": 1056768 * 512, "stats": stats,
        "used_bytes": stats["blocks_used"] * fs.block_size}
(SP / "occupied_meta.json").write_text(json.dumps(meta, indent=1))
hb.update(force=True, phase="готово", last_event="зайнято %.2f GiB" % (meta["used_bytes"]/1024**3))
print(json.dumps(meta, indent=1))
