# Реєстр MQTT-топіків

Єдине місце, де перелічені всі MQTT-топіки, якими користується прошивка —
і загальний клієнт (`mqtt`, `src/main.cpp`), і окремий EcoFlow-клієнт
(`EcoflowClient`). Мета — та сама, що й у `docs/tech_debt.md` для боргу:
одне джерело, а не «шукати по коду, що ми там публікуємо».

**Правило:** будь-яка зміна топіків (новий, видалений, перейменований,
змінений payload/retain/QoS) супроводжується правкою цього файлу в тому ж
коміті — правило зафіксоване в `CLAUDE.md`, розділ «Реєстр MQTT-топіків».

## 0. Префікс — `MqttKeyGenerator`

Загальний клієнт (`mqtt`) додає build-time префікс до кожного топіка через
`MqttKeyGenerator::key()` (`lib/MqttClient/MqttKeyGenerator.{hpp,cpp}`):
`"{prefix}/{topic}"`, провідні/кінцеві `/` зрізаються. Префікс —
`MQTT_TOPIC_PREFIX` (`secrets.ini` → `mqtt_topic_prefix = "mykola-lavryk"`,
`platformio.ini:129`), або runtime-override з `ConfigStorage`
(`CFG_MQTT_TOPIC_PREFIX`, `setupMqttClient()`, `src/main.cpp:1991-2000`).

Нижче в таблицях — **внутрішній** рядок топіка (без префікса, так, як він
виглядає у виклику `addListener()`/`publish()` у коді); реальний рядок на
дроті — `mykola-lavryk/<внутрішній>`, крім EcoFlow (розділ 4 — там
`useKeyGenerator=false`, префікс не застосовується взагалі).

`{client-id}` нижче — `MQTT_CLIENT_ID` (`mqtt-${PIOENV}`, напр.
`mqtt-esp32-c6`).

## 1. Загальний MQTT-клієнт (`mqtt`, `src/main.cpp`)

| Топік (внутрішній) | Payload | Retain | Напрямок | Джерело в коді |
|---|---|---|---|---|
| `devices/{client-id}/status` | текст: `"offline"`/`"online"`/`"heartbeat"` | LWT — ні (`lwtRetain=false`) | плата публікує (LWT + online-publish + heartbeat кожні 5 хв); плата підписується на `devices/+/status` (чужі LWT) | `secrets.ini` (`mqtt_lwt_topic`/`_msg_offline`/`_msg_online`), `src/main.cpp:342,376-381,2029,2066-2070`, `MqttClient.cpp:206-234,268-294` |
| `command/{client-id}` | текст: довільна serial-подібна команда | ні | плата підписується; зовнішній клієнт публікує | `src/main.cpp:2040-2050` |
| `command/{client-id}/reply` | текст: людський лог-вивід порціями (≤512Б×8, з обрізанням) або `"busy: command queue is full"` | ні | плата публікує через `MqttReplyTarget` (`src/main.cpp:665-675`); зовнішній клієнт підписується | `lib/CommandResponse/MqttReplyTarget.hpp`, `lib/CommandResponse/CommandResponse.cpp`, `src/main.cpp:2047-2049` |
| `devices/{client-id}/api/<cmd>` | JSON: `{"id":<num>}` (+ `"args"` для команд з аргументами — наразі жодна) | ні | плата підписується (`addJsonListener`); зовнішній SAPI-клієнт публікує | `registerJsonApiEntry()`, `src/main.cpp:750-758,2054-2062` |
| `devices/{client-id}/api/<cmd>/reply` | JSON: `{"id","ok":true,"data":{...}}` або `{"id","ok":false,"error":"bad args"\|"busy"}` | ні | плата публікує (через `MqttReplyTarget` або пряму відмову) | `handleJsonApiRequest()`, `src/main.cpp:731-742` |
| `devices/{client-id}/ecoflow/<serial>/grid` | JSON: `{"grid","timestamp","charge","remain"}` | **так** | плата публікує (лише на реальний перехід стану мережі) | `src/main.cpp:1351-1371`, `docs/ecoflow-grid-handoff.md` |
| `devices/{client-id}/light-sensor` | текст: число | ні | плата з сенсором публікує; плати без сенсора підписуються на `devices/+/light-sensor` | `src/main.cpp:2075-2089` |
| `console/{client-id}` | текст: один рядок журналу (без `\n`) | ні | плата публікує (rate-limit + allow/deny фільтр по тегах, lossy); зовнішні інструменти підписуються | `lib/ConsoleMqtt/ConsoleMqtt.cpp:224`, лише `HAS_CONSOLE_MQTT`, вимкнено дефолтно (`CONSOLE_MQTT_ACTIVE=0`) |
| `<довільний>` | текст: довільне повідомлення | ні | serial/MQTT-команда `publish <topic> <message>` дозволяє публікацію в будь-який топік | `src/main.cpp:2111-2129` |
| `#` | — | — | внутрішній root-subscribe (лише PicoMQTT-гілка) — плата фільтрує локально, не публічний контракт | `MqttConfig.hpp:38`, `MqttClient.cpp:319-329` |

### JSON API команди фази 1 (`devices/{client-id}/api/<cmd>`)

Allowlist, не дзеркало всіх serial-команд:

| `<cmd>` | Джерело даних | Умова збірки |
|---|---|---|
| `system-info` | `WebSystemModule::chipInfoJson()`/`heapStatsJson()`/`flashStatsJson()`/`nvsStatsJson()`/`partitionsJson()` | `HAS_MQTT_CLIENT && HAS_WEB_PORTAL` |
| `wifi-status` | `WebWifiModule::portalStatusJson()` | — |
| `ecoflow-status` | `WebEcoflowModule::mqttStatusJson()` | + `HAS_ECOFLOW_CLIENT` |

**Заплановано, ще не реалізовано:** опційне поле `replyTopic` у запиті
(відповідь в інший топік замість дефолтного `.../reply`) — обов'язково
має й далі йти через `MqttKeyGenerator`, деталі в
`docs/mqtt-web-handoff.md`.

## 2. Закоментовані/неактивні приклади (не реальний трафік)

Лишені в коді як приклад використання API, обгорнуті в `/* ... */`:
- діагностичний listener на `"#"` (`src/main.cpp:2031-2036`);
- демо-канал `int32/<client-id>` / `int32/#` (`src/main.cpp:2223-2235`).

## 3. Фаза 2 — discovery

`devices/{client-id}/discovery`, retained, republish на кожен
`onConnect()`. **Реалізовано частково** — payload зараз лише
`{"board","revision"}`; поля `features`/`commands` — заплановані, ще не
додані (окремий крок, `src/features.h` X-macro каталог і накопичення
реєстру команд у `registerJsonApiEntry()`). Дизайн —
`docs/mqtt-web-handoff.md`, розділ «Фаза 2».

- `board` — рядок з рукописної мапи `BOARD_XXX` → назва (`src/main.cpp`,
  біля `registerJsonApiEntry()`), не `platformio.ini`'s `board=`.
- `revision` — `GIT_REVISION` (короткий git-sha, `tools/pio_sapi_revision.py`).
- Публікується з `mqtt.onConnect()` — тому **недоступний на ESP8266**:
  PubSubClient-гілка `MqttClient::connect()` не викликає
  `_connected_callback` узагалі (лише PicoMQTT-гілка, `MqttClient.cpp:
  282-294`), не блокер (на ESP8266 і так немає `HAS_WEB_PORTAL`, JSON API
  команд для discovery description поки нема).
- **Не залежить від `HAS_WEB_PORTAL`** (на відміну від JSON API команд
  розділу 1) — `board`/`revision` є build-time константами, не даними з
  `WebSystemModule`/`WebWifiModule`.

## 4. EcoFlow-клієнт (`EcoflowClient`) — окрема схема, без префікса

`EcoflowMqttTopics.hpp` — усі топіки з провідним `/`;
`MqttConfig::useKeyGenerator=false` (`EcoflowClient.cpp:92`) — жодного
`mykola-lavryk/` тут немає. `{account}` — `ecoflow_mqtt_username`
(`secrets.ini`), `{sn}` — серійник пристрою (`EcoflowDeviceRegistry`).

**App Private канал** (реально використовується, акаунт з префіксом `app-`):

| Топік | Призначення | Напрямок |
|---|---|---|
| `/app/device/property/{sn}` | усі поля пристрою (201 у DELTA mini) | плата підписується (`EcoflowClient.cpp:194`) |
| `/app/device/property/#` | root-підписка (локальний фільтр PicoMQTT) | `EcoflowClient::buildRootTopic()` |

**Open Platform канал** (заготовка, `{account}` без `app-`):

| Топік | Призначення |
|---|---|
| `/open/{account}/{sn}/quota` | телеметрія |
| `/open/{account}/{sn}/status` | онлайн/офлайн |
| `/open/{account}/{sn}/set`, `/set_reply` | команда зміни параметра (publish у репо не знайдено) |
| `/open/{account}/{sn}/get`, `/get_reply` | запит значень (publish у репо не знайдено) |
| `/open/{account}/#` | root-підписка (локальний фільтр) |

LWT для EcoFlow-клієнта свідомо не налаштовується — брокер EcoFlow не
дозволяє публікацію в довільні топіки (`EcoflowClient.cpp:100`). Хост —
`mqtt-e.ecoflow.com:8883` (TLS) напряму, або локальний проксі на rpi5
(`ECOFLOW_MQTT_PROXY_*`, plain MQTT у LAN, та сама схема топіків).

## 5. rpi5 — ACL зведення (деталі: `docs/ecoflow_mqtt_proxy_setup.md`, `docs/mqtt-web-handoff.md`)

| Listener | Хто | Права |
|---|---|---|
| `1883` (LAN, `ecoflow-proxy.conf`) | `user esp32-c3` (плата-проксі-споживач) | `read /app/device/property/#` — лише читання |
| `1883` (LAN) | `user <PIOENV>` (загальний клієнт кожної плати) | `readwrite mykola-lavryk/#` |
| `1883` (LAN), анонім | — | лише `read $SYS/#` (після ACL-фіксу 2026-09-22) |
| `1883` (localhost) | локальні інструменти rpi5 | `read $SYS/#`+`mykola-lavryk/#`, `write` у DELTA2-топік |
| `9001` (WebSockets, `sapi.conf`) | `user sapi-<env>` (браузерний SAPI) | `read devices/<client-id>/#` + `write devices/<client-id>/api/+`, без `command/` |
| bridge `ecoflow-proxy` | — | `topic /app/device/property/<SN> in` — по одному на серійник з `EcoflowDeviceRegistry` |

## 6. Секрети/build flags (без значень — самі значення в `secrets.ini`)

| Секрет | Build flag |
|---|---|
| `mqtt_client_id` | `MQTT_CLIENT_ID` (`"mqtt-${PIOENV}"`) |
| `mqtt_lwt_topic` | `MQTT_LWT_TOPIC` |
| `mqtt_lwt_msg_offline`/`_online` | `MQTT_LWT_MSG_OFFLINE`/`_ONLINE` |
| `mqtt_topic_prefix` | `MQTT_TOPIC_PREFIX` |
| `mqtt_username`/`mqtt_password` | `MQTT_USERNAME`/`MQTT_PASSWORD` |
