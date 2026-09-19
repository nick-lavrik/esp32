#!/usr/bin/env bash
# EcoFlow MQTT-проксі: keep-alive (get/latestQuotas) для "тихих" пристроїв
# App Private каналу (DELTA 2) - без нього пристрій публікує дельти лише у
# відповідь на активний запит застосунку, docs/tech_debt.md, "DELTA 2:
# аномально мало MQTT-повідомлень". Запускається systemd timer'ом
# (ecoflow-keepalive.timer) як oneshot-сервіс - один прогін, без власного
# циклу/sleep. Установка - docs/ecoflow_mqtt_proxy_setup.md, крок 10.
# Чому саме такий топік/payload і чому саме такий інтервал -
# docs/tech_debt.md, "DELTA 2: план інтеграції keep-alive (ecoflow-keepalive)".
#
# Змінні оточення (усі опціональні, дефолти - продакшн rpi5):
#   ECOFLOW_KEEPALIVE_HOST       - хост локального listener'а (127.0.0.1)
#   ECOFLOW_KEEPALIVE_PORT       - порт (1883)
#   ECOFLOW_KEEPALIVE_CLIENT_ID  - remote_clientid бриджа (ecoflow-proxy.conf) -
#                                  НЕ ім'я секції `connection` (mosquitto публікує
#                                  $SYS/broker/connection/<X>/state за clientid,
#                                  яким бридж представився ХМАРНОМУ брокеру, а не
#                                  за локальною назвою `connection ecoflow-proxy`
#                                  - перевірено на живому rpi5, 19.09.2026, той
#                                  самий рядок, що й `remote_clientid` нижче).
#   ECOFLOW_KEEPALIVE_TARGETS    - файл зі списком топіків get, по одному на рядок
#                                  (/etc/ecoflow-keepalive/targets.conf)

set -euo pipefail

BROKER_HOST="${ECOFLOW_KEEPALIVE_HOST:-127.0.0.1}"
BROKER_PORT="${ECOFLOW_KEEPALIVE_PORT:-1883}"
BRIDGE_CLIENT_ID="${ECOFLOW_KEEPALIVE_CLIENT_ID:-ANDROID_rpi5bridge_1595134606455103490}"
TARGETS_FILE="${ECOFLOW_KEEPALIVE_TARGETS:-/etc/ecoflow-keepalive/targets.conf}"

log() { printf '%s %s\n' "$(date '+%Y-%m-%d %H:%M:%S')" "$*"; }

if [ ! -f "$TARGETS_FILE" ]; then
    log "ERROR: targets file not found: $TARGETS_FILE"
    exit 1
fi

# $SYS/broker/connection/<remote_clientid>/state - ретейн-повідомлення "1"/"0",
# доступне лише якщо в ecoflow-proxy.conf увімкнено `notifications true` (крок
# 10.1 інструкції) - без нього тут завжди порожньо, і скрипт свідомо мовчки
# пропускає прогін (fail closed: publish у мертвий bridge все одно піде в
# нікуди, а порожній стан не можна відрізнити від "notifications вимкнено").
state="$(mosquitto_sub -h "$BROKER_HOST" -p "$BROKER_PORT" \
    -t "\$SYS/broker/connection/${BRIDGE_CLIENT_ID}/state" -C 1 -W 3 2>/dev/null || true)"

if [ "$state" != "1" ]; then
    log "WARN: bridge '$BRIDGE_CLIENT_ID' state='${state:-<none>}' (expected '1') - skipping, publish would go nowhere"
    exit 0
fi

count=0
while IFS= read -r topic; do
    [ -z "$topic" ] && continue
    case "$topic" in
        \#*) continue ;;
    esac
    id_ms="$(date +%s%3N)"
    payload="{\"id\":${id_ms},\"version\":\"1.0\",\"cmdFunc\":254,\"cmdId\":1,\"params\":{\"operateType\":\"latestQuotas\"}}"
    if mosquitto_pub -h "$BROKER_HOST" -p "$BROKER_PORT" -t "$topic" -m "$payload"; then
        count=$((count + 1))
    else
        log "ERROR: publish failed for topic '$topic'"
    fi
done < "$TARGETS_FILE"

log "INFO: keepalive sent to $count target(s)"
