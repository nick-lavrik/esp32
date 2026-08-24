"""Спільний heartbeat для робочих скриптів: дашборд читає ці файли."""
import json, time
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



class Heartbeat:
    def __init__(self, name, total_bytes=0, min_interval=2.0):
        self.path = SP / f"{name}.heartbeat.json"
        self.state = {"phase": "старт", "ts": time.time(), "done_bytes": 0,
                      "total_bytes": total_bytes, "rate_bps": 0}
        self.started = time.time()
        self.min_interval = min_interval
        self.last_write = 0.0
        self.write(force=True)

    def update(self, force=False, **fields):
        self.state.update(fields)
        self.state["ts"] = time.time()
        elapsed = self.state["ts"] - self.started
        if elapsed > 0:
            # Швидкість рахуємо по ФАКТИЧНО ПРОЧИТАНОМУ, а не по позиції в
            # роботі. Інакше після resume показник злітає до фантастичних
            # значень: уже зроблені шматки "пробігаються" за секунди й
            # враховуються як щойно прочитані, і оцінка часу стає марною.
            # Швидкість - по тому, що прочитано В ЦІЙ сесії (session_bytes),
            # бо байти, успадковані з карти станів після resume, до поточного
            # темпу не належать і завищували б його в разИ.
            # Перевіряємо ПРИСУТНІСТЬ ключа, а не його правдивість: на старті
            # сесії session_bytes дорівнює нулю, і при перевірці через "or"
            # нуль вважався відсутнім значенням - тоді у швидкість потрапляли
            # всі успадковані байти й дашборд показував 1.5 TiB/s.
            if "session_bytes" in self.state:
                progress = self.state["session_bytes"]
            elif "bytes_read" in self.state:
                progress = self.state["bytes_read"]
            else:
                progress = self.state.get("done_bytes", 0)
            self.state["rate_bps"] = progress / elapsed
        self.write(force=force)

    def write(self, force=False):
        now = time.time()
        # Пишемо не частіше за min_interval: heartbeat не має сам себе
        # гальмувати на дрібних операціях.
        if not force and now - self.last_write < self.min_interval:
            return
        self.last_write = now
        tmp = self.path.with_suffix(".tmp")
        tmp.write_text(json.dumps(self.state))
        tmp.replace(self.path)   # атомарна заміна: дашборд не прочитає半-файл
