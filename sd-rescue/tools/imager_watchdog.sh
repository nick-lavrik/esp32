#!/usr/bin/env bash
# Сторож знімання образу: слідкує, щоб imager працював, і перезапускає його
# після падінь.
#
# ЗАПУСКАТИ МОЖНА БУДЬ-КОЛИ, у тому числі коли imager уже працює: сторож
# спершу перевіряє, чи процес живий, і просто чекає. Це навмисно - інакше
# другий imager писав би в той самий образ і ту саму карту станів одночасно,
# і карта перестала б відповідати вмісту.
#
# ЧОГО СТОРОЖ НЕ МОЖЕ: якщо картка залипла (див. README, розділ про стан
# помилки), її оживляє лише зняття живлення плати - тобто людина. У такому
# разі сторож чекає й пише в лог, що потрібне втручання.
set -u

TOOLS="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
IMG="${1:-$HOME/sd-rescue/card.img}"
SRC="${2:-/dev/sda}"
LOG="$TOOLS/../data/watchdog.log"

log() { echo "$(date '+%d.%m %H:%M:%S') $*" >> "$LOG"; }

imager_running() {
  ps -eo comm=,args= | awk '$1 == "python3" && /imager\.py/ {found=1} END {exit !found}'
}

log "сторож запущено (образ $IMG, джерело $SRC)"

while true; do
  # 1. Робота вже завершена?
  if [ -f "$IMG.state" ] && ! grep -qc . /dev/null 2>/dev/null; then
    :   # заглушка: точну перевірку завершення робить imgstat.py
  fi

  # 2. imager живий - нічого не робимо.
  if imager_running; then
    sleep 30
    continue
  fi

  # 3. Пристрій зник - картка залипла, потрібне зняття живлення.
  if [ ! -e "$SRC" ]; then
    log "УВАГА: $SRC зник - картка залипла. Потрібно ЗНЯТИ ЖИВЛЕННЯ плати на 10 с, потім 'sdmsc on'"
    sleep 60
    continue
  fi

  # 4. Пристрій є, imager не працює - запускаємо (resume пропустить зняте).
  log "imager не працює - запускаю (resume продовжить з місця обриву)"
  python3 "$TOOLS/imager.py" --source "$SRC" --out "$IMG" --single-pass --chunk 1048576 \
    >> "$TOOLS/../data/imager_stdout.txt" 2>&1
  code=$?

  if [ $code -eq 0 ]; then
    log "ГОТОВО: образ знято повністю (imager завершився без помилок)"
    exit 0
  fi

  log "imager вийшов з кодом $code - пауза 30 с і повтор"
  sleep 30
done
