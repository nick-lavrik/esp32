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
| `devices/{client-id}/api/<cmd>` | JSON: `{"id":<num>}` (+ `"args"` для команд з аргументами — `ecoflow-journal`: `{"target":"all"\|"<sn>"}`, дефолт `"all"`) | ні | плата підписується (`addJsonListener`); зовнішній SAPI-клієнт публікує | `registerJsonApiEntry()`, `src/main.cpp:750-758,2054-2062` |
| `devices/{client-id}/api/<cmd>/reply` | JSON: `{"id","ok":true,"data":{...}}` або `{"id","ok":false,"error":"bad args"\|"busy"}` | ні | плата публікує (через `MqttReplyTarget` або пряму відмову) | `handleJsonApiRequest()`, `src/main.cpp:731-742` |
| `devices/{client-id}/api/ecoflow-params/<sn>` | JSON: `{"id":<num>}` | ні | плата підписується — ОДНА точна підписка на кожен серійник з `EcoflowDeviceRegistry::deviceTable()` (без wildcard), а не одна на `<cmd>` | `registerEcoflowDeviceParamsEntries()`, `src/main.cpp`, лише `HAS_ECOFLOW_CLIENT` |
| `devices/{client-id}/api/ecoflow-params/<sn>/reply` | JSON: `{"id","ok":true,"data":{"serialNumber","captureAll","droppedParams","params":{...}}}` або `{"id","ok":false,"error":"busy"}` | ні | плата публікує; drill-down на "params" ОДНОГО пристрою — `ecoflow-status` (агрегат вище) їх свідомо не несе (розмір payload) | `WebEcoflowModule::mqttDeviceParamsJson()`, `src/main.cpp` |
| `devices/{client-id}/ecoflow/<serial>/grid` | JSON: `{"grid","timestamp","charge","remain"}` | **так** | плата публікує (лише на реальний перехід стану мережі) | `src/main.cpp:1351-1371`, `docs/ecoflow-grid-handoff.md` |
| `devices/{client-id}/light-sensor` | текст: число | ні | плата з сенсором публікує; плати без сенсора підписуються на `devices/+/light-sensor` | `src/main.cpp:2075-2089` |
| `console/{client-id}` | текст: один рядок журналу (без `\n`) | ні | плата публікує (rate-limit + allow/deny фільтр по тегах, lossy); зовнішні інструменти підписуються | `lib/ConsoleMqtt/ConsoleMqtt.cpp:224`, лише `HAS_CONSOLE_MQTT`, вимкнено дефолтно (`CONSOLE_MQTT_ACTIVE=0`) |
| `<довільний>` | текст: довільне повідомлення | ні | serial/MQTT-команда `publish <topic> <message>` дозволяє публікацію в будь-який топік | `src/main.cpp:2111-2129` |
| `#` | — | — | внутрішній root-subscribe (лише PicoMQTT-гілка) — плата фільтрує локально, не публічний контракт | `MqttConfig.hpp:38`, `MqttClient.cpp:319-329` |

### JSON API команди фази 1 (`devices/{client-id}/api/<cmd>`)

Allowlist, не дзеркало всіх serial-команд:

| `<cmd>` | Джерело даних | Умова збірки |
|---|---|---|
| `system-info` | `WebSystemModule::chipInfoJson()`/`heapStatsJson()`/`flashStatsJson()`/`nvsStatsJson()`/`partitionsJson()` + `WebPortal::statusJson()` | `HAS_MQTT_CLIENT && HAS_WEB_PORTAL` |
| `wifi-status` | `WebWifiModule::portalStatusJson()` | `HAS_MQTT_CLIENT && HAS_WEB_PORTAL` |
| `wifi-connections` | `WebWifiModule::portalConnectionsJson()` (той самий, що `/api/wifi/connections` порталу) — збережені профілі, окремо від `wifi-status` (список міняється рідко, не на кожен статус-тик) | `HAS_MQTT_CLIENT && HAS_WEB_PORTAL` |
| `ecoflow-status` | `WebEcoflowModule::mqttStatusJson()` | `HAS_MQTT_CLIENT && HAS_WEB_PORTAL && HAS_ECOFLOW_CLIENT` |
| `ecoflow-journal` | `WebEcoflowModule::journalJson()` (той самий провайдер, що `/api/ecoflow/journal` порталу, `EcoflowJournalView.hpp`) — окремий запит, не розширення `ecoflow-status` | `HAS_MQTT_CLIENT && HAS_WEB_PORTAL && HAS_ECOFLOW_CLIENT` |
| `mqtt-status` | `WebMqttModule::statusJson()` (те саме, що й `/api/mqtt/status`) | `HAS_MQTT_CLIENT && HAS_WEB_PORTAL` |
| `commands-list` | перелік зареєстрованих serial-команд (`commandHandler.commandName()`/`commandDescription()`), те саме, що й `/api/commands/list` порталу | `HAS_MQTT_CLIENT && !ESP8266` |

**`commands-list` — статичний перелік, не статус.** На відміну від решти
шести (`system-info`/`wifi-status`/`wifi-connections`/`ecoflow-status`/
`ecoflow-journal`/`mqtt-status`), відповідь — `[{"name","description"}, ...]`, не об'єкт
стану; дані не змінюються між перезавантаженнями плати, тому SAPI запитує
їх один раз на конект, а не auto-poll. Джерело для сторінки Commands SAPI
(список команд ліворуч) — див. розділ 5 нижче, де ACL дозволяє SAPI ще й
`command/<id>` (запуск довільної команди).

**`commands-list` — єдина з семи, що не потребує `HAS_WEB_PORTAL`**
(виправлено 2026-09-27, `src/main.cpp`, розділ «MQTT SAPI-канал ... спільна
інфраструктура»): `commandHandler` — глобал файлу, завжди визначений,
незалежно від порталу. Решта шість читають `webPortal`/`webWifiModule`/
`webEcoflowModule`/`webMqttModule` — ці provider-об'єкти самі оголошені
лише під `HAS_WEB_PORTAL` (`src/main.cpp:488-539`), тож на платі без
порталу (напр. `esp32-c3` з `HAS_WEB_PORTAL=0`) discovery публікує
`"commands":["commands-list"]`, а не порожній масив. Повне усунення
залежності решти шести — відкритий борг, `docs/tech_debt.md`, розділ
«Веб-портал через MQTT».

**`ecoflow-params/<sn>` — окремий drill-down, не запис цієї таблиці.**
Своя назва команди (не суфікс на `ecoflow-status`) — навмисно: повертає
інше (сирі `params`, не статус), і жоден зовнішній підписник не сплутає
базову відповідь `ecoflow-status/reply` із цим префіксом (колізія саме
такого роду вже траплялась на живому SAPI, поки команди мали спільний
префікс — виправлено перейменуванням, не костилем у клієнті). Кінцевий
сегмент топіка тут — не сталий на етапі компіляції `<cmd>` (як у всіх
записах вище), а серійний номер ПРИСТРОЮ — реєструється циклом по
`EcoflowDeviceRegistry::deviceTable()`, окремо від `registerJsonApiEntry()`
(розділ 1, рядки `.../ecoflow-params/<sn>` вище).

**`ecoflow-journal` — перша команда фази 1 з реальним `args` у тілі
запиту.** Досі лише шаблон (`docs/mqtt-web-handoff.md`, «Приклад команди з
аргументами») — `{"target":"all"}` чи `{"target":"<sn>"}`, той самий
контракт `sn|index|all`, що серійна команда `ecoflow-journal show`. Не
topic-per-device (як `ecoflow-params/<sn>` вище): запит рідкісний, не
частий per-device polling, статична підписка на кожен пристрій дала б лише
зайві топіки без вигоди. Відповідь — `{"target","rows":[{"atEpoch",
"serialNumber","device","grid","ageSec","mark"}...]}`, `mark` — `">"`/`"<"`/
`""` (найновіший/останній завершений/звичайний рядок, той самий сенс, що
позиційна колонка серійної команди).

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
`onConnect()`. **Реалізовано повністю** — payload
`{"board","revision","features","commands"}`.
Дизайн — `docs/mqtt-web-handoff.md`, розділ «Фаза 2».

- `board` — рядок з рукописної мапи `BOARD_XXX` → назва (`src/main.cpp`,
  біля `registerJsonApiEntry()`), не `platformio.ini`'s `board=`.
- `revision` — `GIT_REVISION` (короткий git-sha, `tools/pio_sapi_revision.py`).
- `features` — масив АКТИВНИХ прапорців з `src/features.h` (повний каталог
  усіх `BOARD_HAS_*`/`HAS_*`, включно з похідними `BOARD_HAS_LIGHT_SENSOR`/
  `HAS_SCREEN_MIRROR` і колишніми self-detecting `HAS_MQTT_CLIENT`/
  `HAS_CONSOLE_MQTT`/`HAS_GMAIL_SENDER`/`HAS_PING`, усі вже переведені на
  явний прапорець у `environment.h` або похідне обчислення в `features.h`),
  рядок — буквальне ім'я макроса (`"BOARD_HAS_DISPLAY"`, не перейменований
  варіант).
- `commands` — масив імен зареєстрованих JSON API команд (`"system-info"`,
  `"wifi-status"`, `"wifi-connections"`, `"ecoflow-status"`, `"ecoflow-journal"`,
  `"mqtt-status"`, `"commands-list"` — залежно від env), джерело —
  `registerJsonApiEntry()` (`src/main.cpp`): той самий виклик, що підписує
  `devices/<client-id>/api/<cmd>` (розділ 1), кладе ім'я в малий fixed-size
  масив (`kJsonApiCommandNames`, без heap). На платі без порталу — `[]`
  (масив і сам накопичувач гейтовані `!ESP8266`, поле лишається постійним
  за формою, не зникає з payload).
- Публікується з `mqtt.onConnect()` — тому **недоступний на ESP8266**:
  PubSubClient-гілка `MqttClient::connect()` не викликає
  `_connected_callback` узагалі (лише PicoMQTT-гілка, `MqttClient.cpp:
  282-294`), не блокер (на ESP8266 і так немає `HAS_WEB_PORTAL`, JSON API
  команд немає взагалі).
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
| `9001` (WebSockets, `sapi.conf`) | `user sapi-<env>` (браузерний SAPI) | `read devices/<client-id>/#` + `write devices/<client-id>/api/+` + `write command/<client-id>` + `read command/<client-id>/reply` (сторінка Commands, сесія 2026-09-26 — розворот попереднього рішення "без `command/`", `docs/mqtt-web-handoff.md`) |
| bridge `ecoflow-proxy` | — | `topic /app/device/property/<SN> in` — по одному на серійник з `EcoflowDeviceRegistry` |

## 6. Секрети/build flags (без значень — самі значення в `secrets.ini`)

| Секрет | Build flag |
|---|---|
| `mqtt_client_id` | `MQTT_CLIENT_ID` (`"mqtt-${PIOENV}"`) |
| `mqtt_lwt_topic` | `MQTT_LWT_TOPIC` |
| `mqtt_lwt_msg_offline`/`_online` | `MQTT_LWT_MSG_OFFLINE`/`_ONLINE` |
| `mqtt_topic_prefix` | `MQTT_TOPIC_PREFIX` |
| `mqtt_username`/`mqtt_password` | `MQTT_USERNAME`/`MQTT_PASSWORD` |
