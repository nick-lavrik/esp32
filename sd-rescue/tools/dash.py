#!/usr/bin/env python3
"""Дашборд стану операції порятунку: що відбувається і чи не зависло.

Запуск:  python3 dash.py          (один знімок)
         python3 dash.py -w       (оновлюється щосекунди)

Головне питання, на яке відповідає дашборд - "чи живий процес". Для цього
кожен робочий скрипт пише heartbeat-файл із часом останньої дії; тут
показується, скільки секунд тому це було. Якщо більше за порог - виводиться
явне попередження, бо зупинений процес і повільний процес виглядають у логах
однаково.
"""
import json, os, sys, time
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


STALL_WARN_SEC = 90

def human(n):
    for u in ("B", "KiB", "MiB", "GiB", "TiB"):
        if abs(n) < 1024:
            return f"{n:.1f} {u}"
        n /= 1024
    return f"{n:.1f} PiB"

def hms(sec):
    sec = int(max(0, sec))
    return f"{sec//3600}г {sec%3600//60:02d}х {sec%60:02d}с"

def procs():
    """Живі python3-процеси наших скриптів."""
    out = []
    for pid in os.listdir("/proc"):
        if not pid.isdigit():
            continue
        try:
            cmd = Path(f"/proc/{pid}/cmdline").read_bytes().decode(errors="replace")
            # Наші процеси - ті, що запущені з sd-rescue/tools (або зі старого
            # scratchpad, якщо щось лишилось з попередніх запусків).
            if "python3" in cmd and ("sd-rescue" in cmd or "scratchpad" in cmd):
                script = next((p.split("/")[-1] for p in cmd.split("\x00")
                               if p.endswith(".py") and "dash.py" not in p), None)
                if script is None:
                    continue   # сам дашборд у списку процесів не потрібен
                stat = Path(f"/proc/{pid}/stat").read_text().split()
                utime = (int(stat[13]) + int(stat[14])) / os.sysconf("SC_CLK_TCK")
                out.append((pid, script, utime))
        except (FileNotFoundError, ProcessLookupError, PermissionError):
            continue
    return out

def device_state(name="sda"):
    """Стан USB-накопичувача, який віддає плата.

    Читаємо /sys, а НЕ сам пристрій: якщо картка залипла, звернення до
    /dev/sda може заблокуватися на десятки секунд, і дашборд - інструмент
    для діагностики - завис би саме тоді, коли він найпотрібніший.

    Нуль у розмірі означає "Media removed": плата віддала хосту, що медіа
    зникло. Це і є головна ознака залипання картки.
    """
    base = Path("/sys/block") / name
    if not base.exists():
        return "ПРИСТРІЙ ЗНИК — картка залипла, потрібне зняття живлення плати"

    try:
        sectors = int((base / "size").read_text().strip())
    except (OSError, ValueError):
        return "стан невідомий (не читається /sys)"

    if sectors == 0:
        return "МЕДІА ВІДСУТНЄ — картка залипла, потрібне зняття живлення плати"

    return f"OK — {sectors * 512 / 1024**3:.1f} GiB, read-only"


def render():
    lines = []
    lines.append("=" * 78)
    lines.append(f" ПОРЯТУНОК SD КАРТКИ — {time.strftime('%H:%M:%S')}")
    lines.append("=" * 78)

    lines.append(f" пристрій : {device_state()}")

    running = procs()
    if running:
        for pid, script, cpu in running:
            lines.append(f" процес   : {script} (PID {pid}, CPU {cpu:.0f}s)")
    else:
        lines.append(" процес   : ЖОДНОГО не запущено")

    # heartbeat кожної операції
    for hb_file in sorted(SP.glob("*.heartbeat.json")):
        try:
            hb = json.loads(hb_file.read_text())
        except (json.JSONDecodeError, FileNotFoundError):
            continue
        age = time.time() - hb.get("ts", 0)
        name = hb_file.name.replace(".heartbeat.json", "")

        # Завершену операцію не можна показувати як зависання: інакше
        # успішно виконаний крок виглядає аварією і відвертає увагу від
        # справжніх проблем.
        finished = hb.get("phase", "").startswith("готово")
        if finished:
            state = "ЗАВЕРШЕНО"
        elif age < STALL_WARN_SEC:
            state = "ЖИВИЙ"
        else:
            state = f"!!! БЕЗ РУХУ {hms(age)} !!!"
        lines.append("")
        lines.append(f" [{name}] {state}   (останній сигнал {age:.0f}s тому)")
        lines.append(f"   фаза       : {hb.get('phase', '?')}")

        done, total = hb.get("done_bytes", 0), hb.get("total_bytes", 0)
        if total:
            pct = 100.0 * done / total
            bar_len = 44
            filled = min(bar_len, int(bar_len * done / total))
            lines.append(f"   [{'#' * filled}{'.' * (bar_len - filled)}] {pct:5.1f}%")
            lines.append(f"   зроблено   : {human(done)} з {human(total)}")

        rate = hb.get("rate_bps", 0)
        if rate:
            lines.append(f"   швидкість  : {human(rate)}/s")

        # ETA рахуємо по ОБСЯГУ, ЯКИЙ ЩЕ ТРЕБА ПРОЧИТАТИ, а не по розміру
        # пристрою. Порожні ділянки пропускаються майже мгновенно, тому
        # оцінка "залишилось" від повного розміру завищувала час удвічі
        # (120 годин замість 64) і робила показник безсенсовним.
        to_read = hb.get("bytes_to_read", 0)
        read_done = hb.get("bytes_read", 0)
        if rate and to_read and read_done < to_read:
            lines.append(f"   прочитано  : {human(read_done)} з {human(to_read)} "
                         f"({100.0 * read_done / to_read:.1f}%)")
            lines.append(f"   залишилось : {hms((to_read - read_done) / rate)}")
        elif rate and total and done < total and not to_read:
            lines.append(f"   залишилось : {hms((total - done) / rate)} (оцінка по розміру пристрою)")
        for k in ("blocks_ok", "blocks_flaky", "blocks_failed", "files_done", "errors"):
            if k in hb:
                lines.append(f"   {k:<11}: {hb[k]}")

        # Зростання цього числа - друга ознака проблем з карткою: сектори
        # читаються, але з помилками. Одиничні значення нормальні, десятки
        # підряд означають, що картка входить у стан помилки.
        if hb.get("blocks_failed", 0) > 0:
            lines.append("   ! є шматки з помилками читання - дивись watchdog.log")
        if hb.get("last_event"):
            lines.append(f"   остання дія: {hb['last_event']}")

    lines.append("")
    lines.append("=" * 78)
    return "\n".join(lines)

if __name__ == "__main__":
    watch = "-w" in sys.argv
    if not watch:
        print(render())
    else:
        try:
            while True:
                print("\033[2J\033[H" + render(), flush=True)
                time.sleep(1)
        except KeyboardInterrupt:
            pass
