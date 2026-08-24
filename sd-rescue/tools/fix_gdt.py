#!/usr/bin/env python3
"""Відновлення таблиці дескрипторів груп (GDT) ext4 з кількох нестабільних читань.

Картка мерехтить окремими бітами: те саме місце читається то як 0, то як 1.
Голосування "за більшістю" тут не працює - у частини бітів розподіл близький
до 50/50. Але кожен дескриптор групи ext4 має власну контрольну суму
(crc32c при metadata_csum), а мерехтливих бітів у 64-байтовому дескрипторі
одиниці. Тому замість вгадування ми ПЕРЕБИРАЄМО всі комбінації мерехтливих
бітів і шукаємо ту єдину, що дає правильний csum - це відновлення точне,
а не ймовірнісне.
"""
import struct, sys, itertools
from collections import Counter
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



# --- crc32c (Castagnoli), та сама форма, що ext4_chksum() у ядрі ------------
_POLY = 0x82F63B78  # відображений 0x1EDC6F41
_TBL = []
for _i in range(256):
    _c = _i
    for _ in range(8):
        _c = (_c >> 1) ^ (_POLY if _c & 1 else 0)
    _TBL.append(_c)

def crc32c(crc, data):
    for b in data:
        crc = (crc >> 8) ^ _TBL[(crc ^ b) & 0xFF]
    return crc & 0xFFFFFFFF

# --- суперблок -------------------------------------------------------------
sb_raw = (SP / "sb1.bin").read_bytes()
sb = sb_raw[1024:2048]

magic = struct.unpack_from("<H", sb, 0x38)[0]
assert magic == 0xEF53, f"не ext4: magic={magic:#x}"

blocks_lo   = struct.unpack_from("<I", sb, 0x04)[0]
blocks_hi   = struct.unpack_from("<I", sb, 0x150)[0]
log_bs      = struct.unpack_from("<I", sb, 0x18)[0]
bpg         = struct.unpack_from("<I", sb, 0x20)[0]
incompat    = struct.unpack_from("<I", sb, 0x60)[0]
ro_compat   = struct.unpack_from("<I", sb, 0x64)[0]
uuid        = sb[0x68:0x78]
desc_size   = struct.unpack_from("<H", sb, 0xFE)[0]
csum_seed_sb = struct.unpack_from("<I", sb, 0x270)[0]

is_64bit       = bool(incompat & 0x0080)
has_csum_seed  = bool(incompat & 0x2000)
metadata_csum  = bool(ro_compat & 0x0400)

blocks = blocks_lo | (blocks_hi << 32 if is_64bit else 0)
block_size = 1024 << log_bs
if desc_size == 0:
    desc_size = 32
groups = (blocks + bpg - 1) // bpg

print(f"block size   : {block_size}")
print(f"blocks       : {blocks}")
print(f"blocks/group : {bpg}")
print(f"groups       : {groups}")
print(f"desc_size    : {desc_size}")
print(f"64bit        : {is_64bit},  metadata_csum: {metadata_csum},  csum_seed feature: {has_csum_seed}")
print(f"UUID         : {uuid.hex()}")

# Насіння csum: при увімкненому csum_seed воно лежить у суперблоці, інакше
# рахується з UUID - так само, як у ext4_fill_super().
seed = csum_seed_sb if has_csum_seed else crc32c(0xFFFFFFFF, uuid)
print(f"csum seed    : {seed:#010x}\n")

def group_csum(group_no, desc):
    """Очікуваний bg_checksum дескриптора - алгоритм ext4_group_desc_csum()."""
    OFFSET_CSUM = 0x1E  # offsetof(struct ext4_group_desc, bg_checksum)
    c = crc32c(seed, struct.pack("<I", group_no))
    c = crc32c(c, desc[:OFFSET_CSUM])
    c = crc32c(c, b"\x00\x00")           # поле csum рахується як нулі
    if len(desc) > OFFSET_CSUM + 2:
        c = crc32c(c, desc[OFFSET_CSUM + 2:])
    return c & 0xFFFF

# --- читання проходів ------------------------------------------------------
passes = [ (SP / f"gdt{i}.bin").read_bytes() for i in range(1, 10) ]
print(f"проходів GDT : {len(passes)}\n")

MAX_BRUTE_BITS = 20   # 2^20 = 1M варіантів; більше - вже не за секунди

stats = Counter()
fixed_gdt = bytearray()
unfixable = []

for g in range(groups):
    off = g * desc_size
    variants = [p[off:off + desc_size] for p in passes]

    # Найчастіший варіант - відправна точка перебору.
    base = Counter(variants).most_common(1)[0][0]

    if len({bytes(v) for v in variants}) == 1:
        # Усі проходи однакові. Перевіряємо csum: якщо він не збігається,
        # пошкодження стабільне (не мерехтіння) і перебором не лікується.
        if not metadata_csum or group_csum(g, base) == struct.unpack_from("<H", base, 0x1E)[0]:
            stats["stable_ok"] += 1
        else:
            stats["stable_bad_csum"] += 1
            unfixable.append((g, "стабільний, але csum невірний"))
        fixed_gdt += base
        continue

    # Позиції бітів, які між проходами відрізняються.
    flaky = []
    for byte_i in range(desc_size):
        diff_mask = 0
        for v in variants:
            diff_mask |= base[byte_i] ^ v[byte_i]
        for bit in range(8):
            if diff_mask & (1 << bit):
                flaky.append((byte_i, bit))

    if len(flaky) > MAX_BRUTE_BITS:
        stats["too_many_bits"] += 1
        unfixable.append((g, f"мерехтить {len(flaky)} бітів - перебір завеликий"))
        fixed_gdt += base
        continue

    # Перебір усіх комбінацій мерехтливих бітів; шукаємо ті, що дають
    # правильний csum. Кілька збігів означали б колізію 16-бітного csum -
    # тоді відповідь неоднозначна, і це треба знати, а не приховувати.
    solutions = []
    for combo in range(1 << len(flaky)):
        cand = bytearray(base)
        for k, (byte_i, bit) in enumerate(flaky):
            if combo & (1 << k):
                cand[byte_i] ^= (1 << bit)
        stored = struct.unpack_from("<H", cand, 0x1E)[0]
        if group_csum(g, bytes(cand)) == stored:
            solutions.append(bytes(cand))
            if len(solutions) > 4:
                break

    if len(solutions) == 1:
        stats["recovered"] += 1
        fixed_gdt += solutions[0]
    elif len(solutions) == 0:
        stats["no_solution"] += 1
        unfixable.append((g, f"{len(flaky)} мерехтливих бітів, жодна комбінація не дала csum"))
        fixed_gdt += base
    else:
        stats["ambiguous"] += 1
        unfixable.append((g, f"{len(solutions)} комбінацій з валідним csum - неоднозначно"))
        fixed_gdt += solutions[0]

print("=== результат ===")
for k, v in stats.most_common():
    print(f"  {k:16s}: {v}")
print(f"  всього груп     : {groups}")

if unfixable:
    print(f"\nпроблемні групи ({len(unfixable)}), перші 15:")
    for g, why in unfixable[:15]:
        print(f"  група {g:5d}: {why}")

out = SP / "gdt_fixed.bin"
out.write_bytes(bytes(fixed_gdt))
print(f"\nвиправлена GDT -> {out} ({len(fixed_gdt)} байт)")
