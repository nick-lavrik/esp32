import os as _os_boot, sys as _sys_boot
_sys_boot.path.insert(0, _os_boot.path.dirname(_os_boot.path.abspath(__file__)))
#!/usr/bin/env python3
"""Знімання образу картки з resume, картою достовірності та heartbeat.

Мета - образ, який можна записати на нову картку і завантажити з нього Pi.
Тому копіюються не файли, а сектори: права, власники, symlink-и, xattr і
таблиця розділів переносяться самі.

ТРИ РЕЧІ, ЯКІ ТУТ ПРИНЦИПОВІ:

1. Копіюється лише ЗАЙНЯТЕ. Розділ на 232 GiB містить ~51 GiB даних, а
   канал віддає близько мегабайта на секунду. Карта зайнятих блоків
   (occupied.bin) береться з bitmap-ів ext4; при будь-якому сумніві щодо
   bitmap-а його група вважається повністю зайнятою.

2. Кожен шматок читається ДВІЧІ. У даних файлів контрольних сум немає, і
   пошкодження в них не детектується зсередини; повторне читання - єдиний
   доступний детектор. Розбіжність не "лікується" голосуванням (на цій
   картці найчастіше значення систематично хибне), а фіксується в карті
   достовірності - щоб потім було видно, яким частинам образу вірити.

3. Resume. Операція триває годинами, і обрив не має означати початок з нуля:
   стан кожного шматка лежить у .state-файлі, при перезапуску вже зроблене
   пропускається.
"""
import os, sys, json, time, argparse, urllib.request
from pathlib import Path
from heartbeat import Heartbeat

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



# Стани шматка в карті достовірності
ST_TODO, ST_OK, ST_FLAKY, ST_ERROR, ST_SKIP = 0, 1, 2, 3, 4
ST_NAMES = {ST_TODO: "не читано", ST_OK: "надійно", ST_FLAKY: "мерехтіло",
            ST_ERROR: "помилка", ST_SKIP: "порожньо"}


class BlockDeviceSource:
    """Читання з блокового пристрою (плата як USB-накопичувач).

    ОБОВ'ЯЗКОВО СКИДАЄ КЕШ після кожного читання. Без цього вся перевірка
    повторюваності перетворюється на самообман: друге читання того самого
    місця приходить з page cache ядра і БУДЬ-ЯКОЛИ збігається з першим, тому
    мерехтіння виглядало б як стабільність. Перевірено на цій картці: три
    читання через кеш дали однаковий md5, а з обходом кешу - чотири різні.

    posix_fadvise(DONTNEED) обрано замість O_DIRECT свідомо: O_DIRECT вимагає
    вирівнювання буфера й довжини, а це в Python окрема морока з ctypes, тоді
    як fadvise дає той самий результат - наступне читання йде до пристрою.
    """
    def __init__(self, path):
        self.path = path
        self.fd = os.open(path, os.O_RDONLY)
        self.size = os.lseek(self.fd, 0, os.SEEK_END)
        os.lseek(self.fd, 0, os.SEEK_SET)

    def read(self, offset, length, drop_cache=True):
        os.lseek(self.fd, offset, os.SEEK_SET)
        buf = b""
        while len(buf) < length:
            chunk = os.read(self.fd, length - len(buf))
            if not chunk:
                break
            buf += chunk
        if drop_cache:
            try:
                os.posix_fadvise(self.fd, offset, length, os.POSIX_FADV_DONTNEED)
            except (AttributeError, OSError):
                pass   # платформа без fadvise - перевірка стане слабшою, не зламаною
        return buf

    def close(self):
        os.close(self.fd)


class HttpSource:
    """Читання через HTTP-сервер на платі (запасний канал, якщо немає USB)."""
    def __init__(self, url, timeout=120):
        self.url = url
        self.timeout = timeout
        req = urllib.request.Request(url, method="HEAD")
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            self.size = int(resp.headers["Content-Length"])

    def read(self, offset, length):
        req = urllib.request.Request(
            self.url, headers={"Range": f"bytes={offset}-{offset + length - 1}"})
        with urllib.request.urlopen(req, timeout=self.timeout) as resp:
            return resp.read()

    def close(self):
        pass


def load_occupied():
    """Карта зайнятих блоків ext4 -> (bitmap, метадані) або (None, None)."""
    bm, meta = SP / "occupied.bin", SP / "occupied_meta.json"
    if not (bm.exists() and meta.exists()):
        return None, None
    return bm.read_bytes(), json.loads(meta.read_text())


def chunk_has_data(occupied, meta, chunk_start, chunk_size):
    """Чи є в цьому шматку хоч один зайнятий блок ФС."""
    if occupied is None:
        return True
    part_off = meta["part_offset"]
    bs = meta["block_size"]
    if chunk_start + chunk_size <= part_off:
        return True                      # MBR і boot-розділ копіюємо повністю
    first_fs_block = max(0, (chunk_start - part_off)) // bs
    last_fs_block = (chunk_start + chunk_size - 1 - part_off) // bs
    for b in range(first_fs_block, min(last_fs_block, meta["blocks"] - 1) + 1):
        if occupied[b >> 3] & (1 << (b & 7)):
            return True
    return False


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--source", required=True, help="/dev/sdX або http://ip:8080/sd.img")
    ap.add_argument("--out", required=True, help="файл образу")
    ap.add_argument("--chunk", type=int, default=1024 * 1024)
    ap.add_argument("--single-pass", action="store_true",
                    help="без другого читання (швидше, але без карти достовірності)")
    ap.add_argument("--limit-bytes", type=int, default=0, help="0 = весь пристрій")
    args = ap.parse_args()

    src = (BlockDeviceSource(args.source) if not args.source.startswith("http")
           else HttpSource(args.source))
    total = args.limit_bytes or src.size
    chunks = (total + args.chunk - 1) // args.chunk

    out_path = Path(args.out)
    state_path = out_path.with_suffix(out_path.suffix + ".state")

    # Sparse-файл: незаповнені (порожні) ділянки не займають місця на диску.
    fd = os.open(out_path, os.O_RDWR | os.O_CREAT)
    os.ftruncate(fd, total)

    state = bytearray(state_path.read_bytes()) if state_path.exists() else bytearray(chunks)
    if len(state) < chunks:
        state.extend(bytearray(chunks - len(state)))

    occupied, meta = load_occupied()
    hb = Heartbeat("imager", total_bytes=total)

    # Скільки байтів реально доведеться прочитати з картки: розмір пристрою
    # для оцінки часу не годиться, бо порожні ділянки пропускаються.
    bytes_to_read = (meta["used_bytes"] + meta["part_offset"]) if meta else total
    hb.update(force=True, phase="готую карту й файл образу", bytes_to_read=bytes_to_read)

    done_before = sum(1 for s in state if s != ST_TODO)
    counts = {ST_OK: 0, ST_FLAKY: 0, ST_ERROR: 0, ST_SKIP: 0}

    # Прочитане рахуємо з КАРТИ СТАНІВ, а не з нуля.
    #
    # НАВІЩО: сторож перезапускає imager після кожного обриву, і при
    # обнуленому лічильнику дашборд щоразу показував "прочитано 2.7 GiB з
    # 124.1" та 70 годин залишку, хоча насправді знято вже 30 GiB. Показник,
    # який після кожного перезапуску починає брехати, гірший за відсутність
    # показника: за ним неможливо зрозуміти, рухається робота чи стоїть.
    #
    # Порожні шматки (ST_SKIP) у прочитане не входять - їх ніхто не читав.
    # Скільки помилок ПІДРЯД означають, що зник пристрій, а не що трапився
    # збійний сектор.
    #
    # НАВІЩО: логіка "збій не має переривати роботу" правильна для окремих
    # секторів, але коли картка залипає, кожне читання повертає помилку - і
    # попередня версія за хвилини "пробігала" решту образу, позначаючи все як
    # прочитане-з-помилкою, після чого рапортувала 100% і код виходу 0.
    # Наслідок: 113 729 шматків (113 GB) вважалися обробленими і більше не
    # перечитувалися б, а сторож завершився з "образ знято повністю".
    MAX_CONSECUTIVE_ERRORS = 40
    consecutive_errors = 0

    already_read_chunks = sum(1 for s in state if s in (ST_OK, ST_FLAKY, ST_ERROR))
    read_bytes = already_read_chunks * args.chunk

    # Швидкість рахуємо лише по цій сесії: успадковані з карти байти прочитані
    # раніше й до поточного темпу не належать.
    session_start_bytes = read_bytes
    hb.update(force=True, phase="продовжую знімання",
              bytes_to_read=bytes_to_read, bytes_read=read_bytes, session_bytes=0)
    start = time.time()

    print(f"джерело : {args.source} ({total/1024**3:.2f} GiB)")
    print(f"образ   : {out_path}")
    print(f"шматків : {chunks} по {args.chunk//1024} KiB, уже зроблено {done_before}")

    for i in range(chunks):
        if state[i] != ST_TODO:
            continue

        offset = i * args.chunk
        length = min(args.chunk, total - offset)

        if not chunk_has_data(occupied, meta, offset, length):
            state[i] = ST_SKIP
            counts[ST_SKIP] += 1
        else:
            try:
                if args.single_pass:
                    # Одне читання, БЕЗ скидання кешу: readahead ядра дає
                    # втричі більшу швидкість (752 KiB/s проти 255), а
                    # порівнювати нам тут нічого.
                    data = src.read(offset, length, drop_cache=False)
                    flaky = False
                    read_bytes += len(data)
                else:
                    # Перевірка повторюваності ОБОВ'ЯЗКОВО зі скиданням кешу,
                    # інакше друге читання приходить з page cache і завжди
                    # збігається з першим (перевірено: три читання через кеш
                    # дали однаковий md5, з обходом кешу - чотири різні).
                    a = src.read(offset, length)
                    b = src.read(offset, length)
                    data, flaky = a, (a != b)
                    read_bytes += len(a) + len(b)

                os.lseek(fd, offset, os.SEEK_SET)
                os.write(fd, data)
                state[i] = ST_FLAKY if flaky else ST_OK
                counts[state[i]] += 1
                consecutive_errors = 0
            except Exception as e:
                # Шматок лишаємо в ST_TODO, а не ST_ERROR: якщо причина -
                # зниклий пристрій, ці дані ще можна прочитати після
                # оживлення картки, і помічати їх як опрацьовані не можна.
                counts[ST_ERROR] += 1
                consecutive_errors += 1
                hb.update(last_event=f"помилка на {offset}: {e}",
                          blocks_failed=counts[ST_ERROR])

                if consecutive_errors >= MAX_CONSECUTIVE_ERRORS:
                    state_path.write_bytes(bytes(state))
                    hb.update(force=True, phase="ПЕРЕРВАНО: пристрій не відповідає",
                              last_event=f"{consecutive_errors} помилок підряд на {offset}")
                    os.close(fd)
                    src.close()
                    print(f"\nПЕРЕРВАНО: {consecutive_errors} помилок підряд - "
                          f"пристрій не відповідає.\n"
                          f"Зніми живлення плати на 10 с, увімкни, виконай 'sdmsc on' "
                          f"- і знімання продовжиться з цього місця.")
                    sys.exit(10)

        if i % 32 == 0 or i == chunks - 1:
            state_path.write_bytes(bytes(state))
            done = sum(1 for s in state if s != ST_TODO)
            hb.update(phase=f"шматок {i+1}/{chunks}",
                      done_bytes=done * args.chunk,
                      total_bytes=total,
                      bytes_to_read=bytes_to_read,
                      bytes_read=read_bytes,
                      session_bytes=read_bytes - session_start_bytes,
                      blocks_ok=counts[ST_OK], blocks_flaky=counts[ST_FLAKY],
                      blocks_failed=counts[ST_ERROR],
                      last_event=f"порожніх пропущено {counts[ST_SKIP]}, "
                                 f"прочитано {read_bytes/1024**2:.0f} MiB")

    state_path.write_bytes(bytes(state))
    os.close(fd)
    src.close()

    remaining = sum(1 for s in state if s == ST_TODO)
    summary = {ST_NAMES[k]: v for k, v in counts.items()}
    hb.update(force=True, phase="готово", last_event=json.dumps(summary, ensure_ascii=False))
    print(f"ГОТОВО за {time.time()-start:.0f}s: {summary}")
    print(f"прочитано з картки: {read_bytes/1024**3:.2f} GiB")

    if remaining:
        # Ненульовий код - щоб сторож НЕ вважав роботу завершеною. Раніше він
        # рапортував "образ знято повністю" при 113 GB непрочитаного.
        print(f"НЕ ЗАВЕРШЕНО: лишилось {remaining} шматків "
              f"({remaining * args.chunk / 1024**3:.1f} GiB)")
        sys.exit(11)


if __name__ == "__main__":
    main()
