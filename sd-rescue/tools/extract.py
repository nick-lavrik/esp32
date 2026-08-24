import os as _os_boot, sys as _sys_boot
_sys_boot.path.insert(0, _os_boot.path.dirname(_os_boot.path.abspath(__file__)))
#!/usr/bin/env python3
"""Витягування файлів з деградованої картки з позначенням недостовірних.

У даних файлів, на відміну від метаданих ext4, контрольних сум НЕМА - тобто
пошкодження в них принципово не детектується зсередини. Єдина доступна
перевірка - прочитати той самий блок двічі: якщо картка віддала різне,
клітинки в цьому місці мерехтять і байтам довіряти не можна.

Тому кожен файл дістає статус:
  clean   - усі блоки прочитались однаково двома проходами;
  suspect - частина блоків мерехтіла (узято найчастіше значення з 5 читань);
  partial - частина блоків недоступна (extent за межі образу тощо).

Читаємо великими послідовними шматками: extent описує сусідні блоки, і один
HTTP-запит на 512 KiB замість 128 окремих на 4 KiB - різниця в часі разів у
десять.
"""
import sys, json, struct, time, os
from collections import Counter
from ext4http import CardReader, Ext4, DEFAULT_SOURCE

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


# Джерело і цілі мусять розбиратися ОКРЕМО. У першій версії перший
# позиційний аргумент ставав водночас і шляхом до образу, і текою для
# витягування - тобто "extract.py /etc" намагався читати образ із /etc.
import argparse

_ap = argparse.ArgumentParser(description="витягування файлів з образу або картки")
_ap.add_argument("targets", nargs="*", default=[],
                 help="шляхи у файловій системі картки (теки або файли)")
_ap.add_argument("--source", default=DEFAULT_SOURCE, help="образ або /dev/sdX")
_ap.add_argument("--dest", default=str(Path.home() / "sd-rescue"),
                 help="куди складати витягнуте")
_ap.add_argument("--verify", action="store_true",
                 help="перечитувати блоки з КАРТКИ двічі й позначати мерехтливі "
                      "(потребує --source /dev/sdX; на образі безсенсовно)")
_ap.add_argument("--symlinks", choices=("real", "text"), default="real",
                 help="real - створювати справжні посилання (типово); "
                      "text - файл з рядком '-> ціль' (зручно для перегляду)")
_opts = _ap.parse_args()

DEST = Path(_opts.dest)
SYMLINK_MODE = _opts.symlinks

# Образ - це вже результат одного читання картки, тому повторне читання з
# нього завжди дасть те саме і нічого не перевірить. Перевіряти дані можна
# лише читаючи ЖИВУ картку двічі (--source /dev/sdX --verify).
SINGLE_READ_SOURCE = not (_opts.verify and not str(_opts.source).endswith(".img"))
TARGETS = _opts.targets or ["/home/nick/Work", "/etc", "/root"]
MAX_SANE = 250 * 1024**3
CHUNK = 512 * 1024
VOTE_PASSES = 5

r = CardReader(_opts.source)
fs = Ext4(r, gdt_override=(SP / "gdt_fixed.bin").read_bytes())
recs = [json.loads(l) for l in (SP / "tree.jsonl").read_text().splitlines() if l.strip()]

manifest = (DEST / "_manifest.jsonl")
DEST.mkdir(parents=True, exist_ok=True)
mf = manifest.open("a")
LOG = DEST / "_progress.txt"

def log(msg):
    with LOG.open("a") as f:
        f.write("%s %s\n" % (time.strftime("%H:%M:%S"), msg))
    print(msg, flush=True)

def read_verified(pos, length):
    """Читає діапазон двічі. -> (дані, чи мерехтіло)

    Голосування тут навмисно НЕ робиться. На цій картці найчастіше значення
    байта систематично хибне (перевірено: правильний байт випадав 7 разів із
    50), тому "відновлення за більшістю" лише створювало б враження
    достовірності. Замість цього беремо перше читання і позначаємо файл як
    suspect - нехай недовіра буде явною.
    """
    a = r.read_raw(pos, length)
    b = r.read_raw(pos, length)
    return a, (a != b)

def read_verified_range(pos, length):
    """Двічі читає діапазон і каже, чи дані повторюються.

    Кеш обходимо обов'язково: без цього друге читання приходить із page cache
    ядра і завжди збігається з першим, тобто перевірка стає самообманом.
    """
    a = r.read_raw(pos, length)
    b = r.read_raw(pos, length)
    return a, (a != b)


def extract_file(rec):
    ino = rec["ino"]
    raw, csum_ok = fs.read_inode(ino, passes=3)
    mode = struct.unpack_from("<H", raw, 0x00)[0]
    size = struct.unpack_from("<I", raw, 0x04)[0] | (struct.unpack_from("<I", raw, 0x6C)[0] << 32)
    flags = struct.unpack_from("<I", raw, 0x20)[0]

    if size > MAX_SANE:
        return {"status": "bad_size", "size": size, "csum": csum_ok}

    rel = rec["path"].lstrip("/")
    dest = DEST / rel

    # Symlink.
    #
    # ext4 тримає ціль двома способами: короткий шлях (< 60 байт) лежить
    # прямо в i_block, довший - у звичайних блоках даних, як у файлі. Перша
    # версія читала лише короткий випадок, а для довгих писала "?" - тобто
    # тихо губила ціль.
    #
    # І створюємо САМЕ ПОСИЛАННЯ, а не текстовий файл з ціллю. Інакше
    # відновлена тека виглядає правдоподібно, але поводиться інакше:
    # /etc/alsa/conf.d/10-rate-lav.conf має вести на /usr/share/..., а не
    # бути файлом з рядком "-> /usr/share/...". Для системи, яку потім
    # запускають, це різні речі.
    if (mode & 0xF000) == 0xA000:
        if size < 60:
            target = raw[0x28:0x28 + 60][:size].split(b"\x00")[0].decode("utf-8", "replace")
        else:
            # Довга ціль лежить у блоці даних. Якщо той блок ще не знято в
            # образ, ціль невідома - і це треба сказати прямо, окремим
            # статусом, а не ховати у загальному "error": після завершення
            # знімання такі посилання просто перечитуються.
            try:
                ext = fs.extents(raw, passes=3) or []
                target_bytes = b""
                for _logical, phys, count in ext:
                    for k in range(count):
                        target_bytes += r.read_raw((phys + k) * fs.block_size, fs.block_size)
                        if len(target_bytes) >= size:
                            break
                target = target_bytes[:size].split(b"\x00")[0].decode("utf-8", "replace")
            except Exception as e:
                return {"status": "symlink_target_unread", "size": size,
                        "csum": csum_ok, "error": str(e)}

            if not target:
                return {"status": "symlink_target_unread", "size": size, "csum": csum_ok}

        dest.parent.mkdir(parents=True, exist_ok=True)

        if dest.is_symlink() or dest.exists():
            dest.unlink()

        if SYMLINK_MODE == "real":
            # Посилання може вести за межі експорту (наприклад у /usr, якого
            # ми не витягували) - це нормально і правильно: ціль збережена
            # такою, якою була на картці.
            os.symlink(target, dest)
        else:
            dest.write_text("-> " + target + "\n")

        return {"status": "symlink", "target": target, "size": size, "csum": csum_ok}

    if (mode & 0xF000) != 0x8000:
        return {"status": "not_regular", "mode": mode, "csum": csum_ok}

    # Inline data - вміст у самому inode, extent-дерева немає.
    if flags & 0x10000000:
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_bytes(raw[0x28:0x28+min(60, size)])
        return {"status": "inline", "size": size, "csum": csum_ok}

    ext = fs.extents(raw, passes=3)
    if ext is None:
        return {"status": "no_extents", "size": size, "csum": csum_ok}

    # Розкладаємо extent'и у карту логічний -> фізичний блок.
    mapping = {}
    for logical, phys, count in ext:
        for k in range(count):
            mapping[logical + k] = phys + k

    # РОЗМІР З INODE - НЕ ІСТИНА. Він читається з тієї самої мерехтливої
    # пам'яті, і один перевернутий біт робить із 26-байтового .gitignore
    # файл на 524 КБ. Гірше: файл на 200 GiB, і тоді витягування забиває
    # диск нулями - саме так у мене з'явилося 188 GiB сміття, після чого
    # закінчилось місце і впало знімання образу.
    #
    # Тому size обмежуємо тим, що ФІЗИЧНО описано extent'ами: більше за
    # виділені блоки файл містити не може. Розбіжність - сама по собі
    # сигнал, що inode побитий, і вона потрапляє в маніфест.
    blocks_allocated = len(mapping)
    max_by_extents = blocks_allocated * fs.block_size
    size_from_inode = size
    size_capped = False

    if size > max_by_extents:
        size = max_by_extents
        size_capped = True

    total_blocks = (size + fs.block_size - 1) // fs.block_size
    dest.parent.mkdir(parents=True, exist_ok=True)

    flaky_blocks = missing_blocks = 0
    written = 0
    with dest.open("wb") as out:
        lb = 0
        while lb < total_blocks:
            # Збираємо максимально довгий послідовний відрізок фізичних блоків.
            if lb not in mapping:
                run = 1
                while lb + run < total_blocks and (lb + run) not in mapping:
                    run += 1
                # Дірку пропускаємо seek-ом, а не записом нулів: файл стає
                # sparse і не займає місця на диску. Різниця не теоретична -
                # запис нулів у файли з побитим розміром і забив диск.
                gap = min(run * fs.block_size, size - written)
                out.seek(gap, os.SEEK_CUR)
                missing_blocks += run
                written += gap
                lb += run
                continue

            start_phys = mapping[lb]
            run = 1
            max_run = CHUNK // fs.block_size
            while (run < max_run and lb + run < total_blocks
                   and mapping.get(lb + run) == start_phys + run):
                run += 1

            length = run * fs.block_size
            try:
                if SINGLE_READ_SOURCE:
                    data = r.read_raw(start_phys * fs.block_size, length)
                    flaky = False
                else:
                    data, flaky = read_verified_range(start_phys * fs.block_size, length)
            except Exception as e:
                data, flaky = b"\x00" * length, False
                missing_blocks += run
                log("  блок %d (%s): %s" % (start_phys, rec["path"], e))
            if flaky:
                flaky_blocks += run

            take = min(length, size - written)
            out.write(data[:take])
            written += take
            lb += run

    # Файл мусить мати рівно size байтів: seek-ом наприкінці розмір не
    # виставляється, тому дотягуємо явно.
    with dest.open("r+b") as fix:
        fix.truncate(size)

    # ЧОМУ НЕ "clean": цей статус означає лише, що всі блоки файлу присутні
    # в джерелі й контрольна сума inode валідна. Він НІЧОГО не каже про самі
    # дані - у даних файлів ext4 контрольних сум немає, а образ знімався
    # одним проходом, тобто мерехтіння бітів у них не детектувалося.
    #
    # Реальний випадок, який змусив це переназвати: /etc/auto.sshfs мав
    # статус "clean", а всередині "/iome/nick", "nack", "it_rca" - тобто
    # текст із перевернутими бітами. Статус, який читається як "все добре",
    # гірший за відсутність статусу: він знімає підозру там, де вона потрібна.
    status = "blocks_present"
    if missing_blocks:
        status = "partial"
    elif flaky_blocks:
        status = "data_flaky"
    if size_capped or not csum_ok:
        status = "suspect_size"

    return {"status": status,
            # Чи перевірялися САМІ ДАНІ на повторюваність. False означає:
            # блоки на місці, але чи не мерехтять байти - невідомо.
            "data_verified": not SINGLE_READ_SOURCE,
            "size": size, "size_from_inode": size_from_inode,
            "size_capped": size_capped, "blocks": total_blocks,
            "blocks_allocated": blocks_allocated,
            "flaky_blocks": flaky_blocks, "missing_blocks": missing_blocks,
            "csum": csum_ok}

# --- прохід -----------------------------------------------------------------
todo = []
for t in TARGETS:
    todo += [x for x in recs if x["k"] in ("f", "l") and (x["path"] == t or x["path"].startswith(t + "/"))]
seen = set()
todo = [x for x in todo if not (x["path"] in seen or seen.add(x["path"]))]

# Запобіжник: якщо на диску залишається менше цього - припиняємо. Витягування
# з побитими розмірами вже одного разу забило диск під нуль і поклало
# знімання образу; краще зупинитись і сказати про це.
MIN_FREE_BYTES = 5 * 1024**3

log("витягую %d об'єктів у %s" % (len(todo), DEST))
stats = Counter()
start = time.time()

for i, rec in enumerate(todo, 1):
    st = os.statvfs(DEST)
    if st.f_bavail * st.f_frsize < MIN_FREE_BYTES:
        log("СТОП: на диску менше %d GiB вільного місця" % (MIN_FREE_BYTES // 1024**3))
        break

    try:
        res = extract_file(rec)
    except Exception as e:
        res = {"status": "error", "error": str(e)}
    res["path"] = rec["path"]
    res["ino"] = rec["ino"]
    stats[res["status"]] += 1
    mf.write(json.dumps(res) + "\n")
    if i % 25 == 0:
        mf.flush()
        log("%d/%d | %s | %.1f MiB прочитано, %.0fs"
            % (i, len(todo), dict(stats), r.bytes_read/1048576, time.time()-start))

mf.close()
log("ГОТОВО %s за %.0fs, прочитано %.1f MiB" % (dict(stats), time.time()-start, r.bytes_read/1048576))
