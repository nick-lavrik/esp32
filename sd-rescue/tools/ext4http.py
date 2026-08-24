#!/usr/bin/env python3
"""Читання ext4 напряму з HTTP-сервера на ESP32, в обхід ядра.

НАВІЩО НЕ ЯДРО: ядро відмовляється монтувати ФС, поки ХОЧ ОДИН дескриптор
групи невалідний, а на цій картці незворотно побито 25% дескрипторів (усі -
у хвості, після 117 GiB). Але дерево каталогів системи живе на початку
розділу, де все ціле. Власний парсер читає лише ті блоки, які реально
потрібні, і не спотикається про пошкодження в місцях, куди не заглядає.

ЯК ЛІКУЄМО МЕРЕХТІННЯ: блок, у якого не збігається вбудована контрольна
сума (inode, каталог, extent-блок), перечитується кілька разів; варіанти
порівнюються між собою, а мерехтливі біти перебираються, доки csum не
співпаде. Це те саме, що дало відновлення GDT.
"""
import struct, sys, os, json, urllib.request, itertools
from collections import Counter

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


# Джерело за замовчуванням - локальний образ. HTTP на плату лишається
# підтримуваним (CardReader розрізняє за префіксом), але типовий шлях тепер
# інший: картка віддається хостом як USB-накопичувач, а дерево зручніше
# читати з уже знятого образу - це не конкурує з imager за пристрій.
DEFAULT_SOURCE = os.path.expanduser("~/sd-rescue/card.img")
BASE_URL = DEFAULT_SOURCE
PART_OFFSET = 1056768 * 512      # початок ext4-розділу на картці
HTTP_TIMEOUT = 120

# --- crc32c ----------------------------------------------------------------
_POLY = 0x82F63B78
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


class CardReader:
    """Читання діапазонів картки з кешем у пам'яті.

    Джерело - або HTTP-сервер на платі, або блоковий пристрій (плата як
    USB-накопичувач). Для пристрою після кожного читання скидається page
    cache: інакше повторне читання того самого місця приходить з кешу ядра, і
    перевірка повторюваності перестає щось перевіряти.
    """

    def __init__(self, url=BASE_URL, offset=PART_OFFSET):
        self.url = url
        self.offset = offset
        self.device_fd = None
        if not str(url).startswith("http"):
            self.device_fd = os.open(str(url), os.O_RDONLY)
        self.cache = {}
        self.http_reads = 0
        self.bytes_read = 0
        self.repaired_blocks = 0
        self.failed_blocks = 0
        self.repair_log = []

    def read_raw(self, pos, length):
        """Одне читання діапазону розділу (без кешу самого класу)."""
        start = self.offset + pos

        if self.device_fd is not None:
            os.lseek(self.device_fd, start, os.SEEK_SET)
            buf = b""
            while len(buf) < length:
                chunk = os.read(self.device_fd, length - len(buf))
                if not chunk:
                    break
                buf += chunk
            try:
                os.posix_fadvise(self.device_fd, start, length, os.POSIX_FADV_DONTNEED)
            except (AttributeError, OSError):
                pass
            self.http_reads += 1
            self.bytes_read += len(buf)
            if len(buf) != length:
                raise IOError(f"очікував {length} байт, отримав {len(buf)}")
            return buf

        req = urllib.request.Request(
            self.url, headers={"Range": f"bytes={start}-{start + length - 1}"})
        with urllib.request.urlopen(req, timeout=HTTP_TIMEOUT) as resp:
            data = resp.read()
        self.http_reads += 1
        self.bytes_read += len(data)
        if len(data) != length:
            raise IOError(f"очікував {length} байт, отримав {len(data)}")
        return data

    def read_block(self, block_no, block_size, passes=1, validator=None):
        """Блок з кешу або з картки.

        validator(data) -> bool перевіряє вбудовану контрольну суму блоку.
        Якщо вона не сходиться, блок перечитується до `passes` разів, а потім
        мерехтливі біти перебираються - так само, як при відновленні GDT.
        """
        key = (block_no, block_size)
        if key in self.cache:
            return self.cache[key]

        pos = block_no * block_size
        variants = [self.read_raw(pos, block_size)]

        if validator is None or validator(variants[0]):
            self.cache[key] = variants[0]
            return variants[0]

        # csum не збігся - перечитуємо.
        for _ in range(passes - 1):
            data = self.read_raw(pos, block_size)
            if validator(data):
                self.cache[key] = data
                return data
            variants.append(data)

        repaired, how, spent = self._brute_force(variants, validator)
        if repaired is not None:
            self.repaired_blocks += 1
            self.repair_log.append((block_no, how, spent))
            self.cache[key] = repaired
            return repaired

        self.failed_blocks += 1
        self.repair_log.append((block_no, how, spent))
        # Відновити не вдалося - повертаємо НАЙЧАСТІШИЙ варіант, а не перший.
        # Різниця не косметична: перше читання може мати мерехтливий байт у
        # критичному полі (реальний випадок - розмір файлу прочитався як
        # 524346 замість 26, хоча 15 наступних читань давали 26), і тоді
        # помилка тихо потрапляє в результат.
        from collections import Counter as _C
        best = _C(variants).most_common(1)[0][0]
        self.cache[key] = best
        return best

    @staticmethod
    def _brute_force(variants, validator, max_bits=12):
        """Відновлення блоку з кількох нестабільних читань.

        Наївний перебір усіх мерехтливих бітів тут не працює: у 4-КБ блоці
        каталогу мерехтить близько 150 байтів, тобто 2^150 комбінацій. Тому
        двоступенево:

          1) ГОЛОСУВАННЯ по бітах. Після 15+ читань більшість мерехтливих
             бітів має чітку перевагу - їх значення визначається одразу.
          2) ПЕРЕБІР лише тих бітів, де голоси розділилися майже навпіл
             (таких одиниці), з перевіркою контрольної суми блоку.

        Саме csum робить результат перевіреним, а не ймовірнісним: якщо він
        збігся, блок відновлено точно.
        """
        length = len(variants[0])
        passes = len(variants)

        ones = bytearray(length * 8)
        for v in variants:
            for i in range(length):
                b = v[i]
                if b:
                    base_i = i * 8
                    for bit in range(8):
                        if (b >> bit) & 1:
                            ones[base_i + bit] += 1

        voted = bytearray(length)
        uncertain = []
        for i in range(length):
            value = 0
            for bit in range(8):
                cnt = ones[i * 8 + bit]
                if cnt * 2 > passes:
                    value |= 1 << bit
                # Перевага менша за 2/3 - біт спірний, віддаємо його переборові.
                if cnt and cnt != passes and max(cnt, passes - cnt) * 3 < passes * 2:
                    uncertain.append((i, bit))
            voted[i] = value

        if validator(bytes(voted)):
            return bytes(voted), "голосування", len(uncertain)

        if len(uncertain) > max_bits:
            return None, f"спірних бітів {len(uncertain)} - перебір завеликий", len(uncertain)

        for combo in range(1 << len(uncertain)):
            cand = bytearray(voted)
            for k, (i, bit) in enumerate(uncertain):
                if combo & (1 << k):
                    cand[i] ^= 1 << bit
            if validator(bytes(cand)):
                return bytes(cand), "голосування+перебір", len(uncertain)

        return None, "csum не збігся", len(uncertain)


class Ext4:
    def __init__(self, reader, gdt_override=None, sb_attempts=9):
        self.r = reader

        # Суперблок читаємо з повторами, доки не побачимо магію 0xEF53.
        #
        # НАВІЩО: одноразове читання тут ламало весь скрипт випадковим чином.
        # На цій картці мерехтить будь-який байт, і коли мерехтіння падає саме
        # на поле magic, ФС виглядає "не ext4" - хоча наступне читання того
        # самого місця дає правильні дані. Це не теоретичний ризик: саме так
        # обвалився прохід карти зайнятих блоків після сорока хвилин роботи.
        sb = None
        for _attempt in range(sb_attempts):
            sb_area = reader.read_raw(0, 4096)
            candidate = sb_area[1024:2048]
            if struct.unpack_from("<H", candidate, 0x38)[0] == 0xEF53:
                sb = candidate
                break

        if sb is None:
            raise IOError(f"суперблок не прочитався з {sb_attempts} спроб "
                          f"(магія 0xEF53 не знайдена) - або це не ext4, "
                          f"або зміщення розділу невірне")

        self.inodes_count      = struct.unpack_from("<I", sb, 0x00)[0]
        blocks_lo             = struct.unpack_from("<I", sb, 0x04)[0]
        self.first_data_block = struct.unpack_from("<I", sb, 0x14)[0]
        log_bs                = struct.unpack_from("<I", sb, 0x18)[0]
        self.blocks_per_group = struct.unpack_from("<I", sb, 0x20)[0]
        self.inodes_per_group = struct.unpack_from("<I", sb, 0x28)[0]
        self.inode_size       = struct.unpack_from("<H", sb, 0x58)[0]
        self.incompat         = struct.unpack_from("<I", sb, 0x60)[0]
        self.ro_compat        = struct.unpack_from("<I", sb, 0x64)[0]
        self.uuid             = sb[0x68:0x78]
        desc_size             = struct.unpack_from("<H", sb, 0xFE)[0]

        self.block_size = 1024 << log_bs
        self.is_64bit = bool(self.incompat & 0x0080)
        self.metadata_csum = bool(self.ro_compat & 0x0400)
        self.desc_size = desc_size if desc_size else 32
        self.blocks = blocks_lo | (struct.unpack_from("<I", sb, 0x150)[0] << 32
                                   if self.is_64bit else 0)
        self.groups = (self.blocks + self.blocks_per_group - 1) // self.blocks_per_group
        self.csum_seed = crc32c(0xFFFFFFFF, self.uuid)

        # GDT: беремо вже виправлену версію, якщо передана.
        if gdt_override:
            self.gdt = gdt_override
        else:
            gdt_bytes = self.groups * self.desc_size
            blocks_needed = (gdt_bytes + self.block_size - 1) // self.block_size
            self.gdt = reader.read_raw((self.first_data_block + 1) * self.block_size,
                                       blocks_needed * self.block_size)

    def group_desc(self, group):
        off = group * self.desc_size
        return self.gdt[off:off + self.desc_size]

    def inode_table_block(self, group):
        d = self.group_desc(group)
        lo = struct.unpack_from("<I", d, 0x08)[0]
        hi = struct.unpack_from("<I", d, 0x28)[0] if (self.is_64bit and self.desc_size >= 0x2C) else 0
        return lo | (hi << 32)

    def _inode_csum_ok(self, ino, raw):
        """Перевірка i_checksum_lo/hi інода (ext4_inode_csum)."""
        if not self.metadata_csum or self.inode_size < 0x82:
            return True
        gen = struct.unpack_from("<I", raw, 0x64)[0]
        c = crc32c(self.csum_seed, struct.pack("<I", ino))
        c = crc32c(c, struct.pack("<I", gen))
        body = bytearray(raw)
        struct.pack_into("<H", body, 0x7C, 0)          # i_checksum_lo
        if self.inode_size > 128:
            extra = struct.unpack_from("<H", raw, 0x80)[0]
            if extra >= 0x1C + 2:                       # є i_checksum_hi
                struct.pack_into("<H", body, 0x82, 0)
        c = crc32c(c, bytes(body))
        stored = struct.unpack_from("<H", raw, 0x7C)[0]
        return (c & 0xFFFF) == stored

    def read_inode(self, ino, passes=9):
        group = (ino - 1) // self.inodes_per_group
        index = (ino - 1) % self.inodes_per_group
        table = self.inode_table_block(group)
        byte_off = index * self.inode_size
        block = table + byte_off // self.block_size
        off_in_block = byte_off % self.block_size

        def validator(data):
            return self._inode_csum_ok(ino, data[off_in_block:off_in_block + self.inode_size])

        # Валідатор дивиться лише на потрібний інод, але перебір іде по всьому
        # блоку - інші іноди в тому ж блоці нас тут не цікавлять.
        data = self.r.read_block(block, self.block_size, passes=passes, validator=validator)
        raw = data[off_in_block:off_in_block + self.inode_size]
        return raw, validator(data)

    def extents(self, inode_raw, passes=9):
        """Список (logical_block, physical_block, count) з extent-дерева."""
        i_block = inode_raw[0x28:0x28 + 60]
        flags = struct.unpack_from("<I", inode_raw, 0x20)[0]
        if not (flags & 0x80000):        # EXT4_EXTENTS_FL
            return None                   # старі block maps тут не потрібні
        return self._extent_node(i_block, passes)

    def _extent_node(self, node, passes):
        magic, entries, _max, depth, _gen = struct.unpack_from("<HHHHI", node, 0)
        if magic != 0xF30A:
            return []
        out = []
        for i in range(entries):
            e = 12 + i * 12
            if depth == 0:
                logical, length, hi, lo = struct.unpack_from("<IHHI", node, e)
                phys = lo | (hi << 32)
                if length > 32768:        # uninitialized extent
                    length -= 32768
                out.append((logical, phys, length))
            else:
                logical, lo, hi, _unused = struct.unpack_from("<IIHH", node, e)
                child = lo | (hi << 32)
                data = self.r.read_block(child, self.block_size, passes=passes)
                out.extend(self._extent_node(data[:], passes))
        return out

    def read_file_blocks(self, inode_raw, passes=9):
        for logical, phys, count in (self.extents(inode_raw, passes) or []):
            for k in range(count):
                yield logical + k, phys + k

    def inode_csum_seed(self, ino, inode_raw):
        """i_csum_seed інода: з нього рахуються суми його блоків каталогу."""
        gen = struct.unpack_from("<I", inode_raw, 0x64)[0]
        c = crc32c(self.csum_seed, struct.pack("<I", ino))
        return crc32c(c, struct.pack("<I", gen))

    def dir_block_validator(self, seed):
        """Перевірка ext4_dir_entry_tail - контрольної суми блоку каталогу.

        Хвіст займає останні 12 байтів блоку і має характерний вигляд:
        inode=0, rec_len=12, name_len=0, filetype=0xDE. Сума рахується по
        блоку БЕЗ цих 12 байтів. Якщо хвоста немає (ФС без metadata_csum
        або старий каталог), перевіряти нічого - вважаємо блок дійсним.
        """
        def validate(data):
            if len(data) < 12:
                return True
            tail = data[-12:]
            zero1, rec_len, _z2, ft = struct.unpack_from("<IHBB", tail, 0)
            if not (zero1 == 0 and rec_len == 12 and ft == 0xDE):
                return True  # хвоста немає - нічого перевіряти
            stored = struct.unpack_from("<I", tail, 8)[0]
            return crc32c(seed, data[:-12]) == stored
        return validate

    def iter_dir(self, inode_raw, passes=9, ino=None):
        """Записи каталогу: (inode, type, name)."""
        size = struct.unpack_from("<I", inode_raw, 0x04)[0]
        seen = 0
        # Блоки каталогу мерехтять так само, як усе інше, і без перевірки суми
        # в listing потрапляють побиті імена ("`roc" замість "proc"). Тому
        # кожен блок валідується власним хвостом-контрольною сумою.
        validator = (self.dir_block_validator(self.inode_csum_seed(ino, inode_raw))
                     if (self.metadata_csum and ino is not None) else None)
        for _logical, phys in self.read_file_blocks(inode_raw, passes):
            if seen >= size:
                break
            data = self.r.read_block(phys, self.block_size, passes=passes,
                                     validator=validator)
            seen += self.block_size
            pos = 0
            while pos < self.block_size - 8:
                ino, rec_len, name_len, ftype = struct.unpack_from("<IHBB", data, pos)
                if rec_len < 8 or pos + rec_len > self.block_size:
                    break
                if ino != 0 and name_len:
                    name = data[pos + 8: pos + 8 + name_len]
                    try:
                        yield ino, ftype, name.decode("utf-8")
                    except UnicodeDecodeError:
                        yield ino, ftype, name.decode("latin-1") + "  [?]"
                pos += rec_len
