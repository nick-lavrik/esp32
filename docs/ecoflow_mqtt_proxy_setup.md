# EcoFlow MQTT-проксі: налаштування з нуля на новому сервері

Покрокова інструкція для відтворення проксі (стадія 1 `docs/tech_debt.md`,
розділ "MQTT-проксі") на будь-якому Linux-хості в тій самій LAN, що й плата
без PSRAM (`esp32-c3`). Навіщо це взагалі потрібно, які виміри це
підтвердили і які рішення ще відкриті — `docs/tech_debt.md`, розділ
"MQTT-проксі: винести TLS з ESP32 на зовнішній хост". Цей документ — лише
"як зробити", без "чому".

**Суть в одному реченні:** сервер тримає ОДНЕ TLS-з'єднання до хмари EcoFlow
(mosquitto bridge — це власна термінологія mosquitto, не плутати з нашим
"proxy" нижче) і ретранслює телеметрію в LAN відкритим текстом; плата без
PSRAM економить ~50 КБ heap на mbedTLS-буферах, під'єднуючись до сервера
звичайним plain MQTT.

## Передумови

- Linux-хост у тій самій LAN, що й плата, з передбачуваною IP-адресою
  (статична або DHCP-резервація — адреса йде в прошивку як build flag,
  міняти її після цього незручно).
- `apt`-based дистрибутив (Debian/Ubuntu — перевірено на Debian 13 trixie,
  mosquitto 2.0.21-1 з основного репозиторію; на інших дистрибутивах пакет
  може називатись інакше, кроки 2+ незмінні).
- SSH-доступ з правами `sudo`.
- Діючий акаунт EcoFlow App із уже випущеними MQTT-креденшелами
  (`ecoflow_mqtt_username`/`ecoflow_mqtt_password`/`ecoflow_user_id` у
  `secrets.ini` цього репозиторію — якщо їх ще нема, спершу
  `docs/ecoflow.md`, розділ "Автентифікація" → "Приватний API").

## Крок 1 — встановити mosquitto

```sh
sudo apt update
sudo apt install mosquitto mosquitto-clients
sudo systemctl enable --now mosquitto
```

Пакетний `/etc/mosquitto/mosquitto.conf` за замовчуванням уже містить
`include_dir /etc/mosquitto/conf.d` — усе наше йде окремим файлом туди,
головний конфіг не чіпаємо взагалі.

## Крок 2 — зібрати вхідні дані

З `secrets.ini` цього репозиторію (той самий акаунт, що вже використовує
прошивка):

| Значення | Звідки | Куди піде |
| :--- | :--- | :--- |
| `ecoflow_mqtt_username` | `secrets.ini` | `remote_username` |
| `ecoflow_mqtt_password` | `secrets.ini` | `remote_password` |
| `ecoflow_user_id` | `secrets.ini` | хвіст `remote_clientid` |
| `ecoflow_mqtt_host`/`_port` | `secrets.ini` (зазвичай `mqtt-e.ecoflow.com`/`8883`) | `address` |

Серійні номери пристроїв — `src/Ecoflow/EcoflowDeviceRegistry.cpp`, масив
`kDevices` (перше поле кожного запису).

LAN-адреса сервера і порт listener'а — на власний розсуд (у продакшні
цього репозиторію: `192.168.1.22:1883`, той самий хост, що вже слухає інший
трафік — mosquitto тримає окремий `listener` на власному порту, конфлікту
нема).

## Крок 3 — локальні LAN-креденшли проксі (НЕ облікові дані EcoFlow)

Це окремий, суто внутрішній логін/пароль — плата ходить ними на локальний
`listener`, а не на хмару EcoFlow. Довільні, але не порожні:

```sh
mkdir -p ~/scratch && cd ~/scratch
openssl rand -base64 18 > proxy_pw.txt
scp proxy_pw.txt <server>:/tmp/proxy_pw.txt
```

На сервері:

```sh
PW=$(cat /tmp/proxy_pw.txt)
sudo mosquitto_passwd -b -c /etc/mosquitto/ecoflow_proxy_passwd esp32-c3 "$PW"
unset PW
shred -u /tmp/proxy_pw.txt
```

`-c` створює файл заново — на вже наявному listener'і з кількома платами
`-c` пропускають (він перезаписав би файл, стерши решту користувачів) і
викликають `mosquitto_passwd -b <file> <user> <pass>` без `-c`.

Пароль з `proxy_pw.txt` іде і в `secrets.ini` цього репозиторію
(`ecoflow_proxy_password`) — те саме значення в обох місцях, інакше плата
не авторизується. `shred -u` локальну копію одразу після цього.

## Крок 4 — ACL: лише читання, лише потрібний топік

```sh
sudo tee /etc/mosquitto/ecoflow_proxy_acl >/dev/null <<'EOF'
# MQTT-проксі: плата лише читає телеметрію, ніколи не публікує.
user esp32-c3
topic read /app/device/property/#
EOF
```

**Чому не ширший wildcard.** `EcoflowMqttTopics` (`src/Ecoflow/`) для
App Private каналу (акаунт з префіксом `app-`) використовує рівно
`/app/device/property/{sn}` — жоден інший топік платі не потрібен.
Для Open Platform каналу (акаунт `open-...`) схема інша
(`/open/{account}/{sn}/...`) — підправити ACL і `topic ... in` нижче
відповідно, якщо акаунт саме такий (`docs/ecoflow.md`, "Два канали").

## Крок 5 — права на файли (типова пастка)

mosquitto скидає привілеї на системного користувача `mosquitto` **до**
відкриття `password_file`/`acl_file` (на відміну від головного `.conf`,
який читається ще під root) — файли мають належати рівно
`mosquitto:mosquitto`, інакше брокер впаде з `EACCES` (exit 13) при
старті, навіть якщо root сам файл читає без проблем:

```sh
sudo chown mosquitto:mosquitto /etc/mosquitto/ecoflow_proxy_passwd /etc/mosquitto/ecoflow_proxy_acl
sudo chmod 640 /etc/mosquitto/ecoflow_proxy_passwd /etc/mosquitto/ecoflow_proxy_acl
```

`root:mosquitto` теж якийсь час працює, але з 2.0.21 і вище видає
deprecation warning і в майбутньому перестане — одразу ставити власника
`mosquitto:mosquitto`, без проміжних варіантів.

## Крок 6 — конфіг проксі

```sh
sudo tee /etc/mosquitto/conf.d/ecoflow-proxy.conf >/dev/null <<'EOF'
# EcoFlow MQTT-проксі (docs/ecoflow_mqtt_proxy_setup.md): одна TLS-сесія до
# хмари EcoFlow сюди, plain MQTT для довіреної LAN назовні.

listener 1883 <IP-ЦЬОГО-СЕРВЕРА-В-LAN>
allow_anonymous false
password_file /etc/mosquitto/ecoflow_proxy_passwd
acl_file /etc/mosquitto/ecoflow_proxy_acl

connection ecoflow-proxy
address mqtt-e.ecoflow.com:8883
remote_username <ecoflow_mqtt_username>
remote_password <ecoflow_mqtt_password>
remote_clientid ANDROID_<будь-який-впізнаваний-рядок>_<ecoflow_user_id>
bridge_cafile /etc/ssl/certs/ca-certificates.crt
bridge_protocol_version mqttv311
cleansession true
notifications false
try_private false
start_type automatic
restart_timeout 10
topic /app/device/property/<SN-1> in
topic /app/device/property/<SN-2> in
EOF
```

Заповнити `<...>` значеннями з кроку 2 (по одному `topic ... in` на кожен
серійний номер з `kDevices`, `EcoflowDeviceRegistry.cpp`). Директиви `bridge_cafile`,
`remote_username`, `remote_password`, `remote_clientid`, `connection` —
буквальний синтаксис самого mosquitto, назви не міняти.

**`remote_clientid` має суворий формат.** Перевірено на живому брокері:
префікс `ANDROID_` і справжній `ecoflow_user_id` у кінці — обов'язкові;
середина довільна, але УНІКАЛЬНА в межах акаунта — якщо той самий
`clientId` вже використовує production-плата (`buildClientId()`,
`src/Ecoflow/EcoflowClient.cpp`) чи офіційний застосунок, брокер вибиватиме
клієнтів по черзі нескінченним reconnect-циклом. `"IOS_"` замість
`"ANDROID_"` або чужий `userId` — миттєвий `Connection Refused: not
authorised`.

## Крок 7 — запустити і перевірити

```sh
sudo systemctl restart mosquitto
sudo systemctl status mosquitto --no-pager
sudo tail -30 /var/log/mosquitto/mosquitto.log
```

У логу очікується (без помилок і попереджень):

```
Opening ipv4 listen socket on port 1883.
Connecting bridge (step 1) ecoflow-proxy (mqtt-e.ecoflow.com:8883)
mosquitto version 2.0.21 running
```

Якщо натомість `Error: Unable to open pwfile ...` або `EACCES` — крок 5
(права на файли) не виконано або виконано на не тих файлах; `strace -f -e
trace=setuid,setgid,openat sudo mosquitto -c /etc/mosquitto/mosquitto.conf`
одразу показує момент `setuid`/`setgid` перед провальним `openat`.

## Крок 8 — ручна перевірка з іншої машини в LAN

```sh
# Анонімно - має впасти:
mosquitto_sub -h <IP-сервера> -t '/app/device/property/#' -v
# → Connection Refused: not authorised.

# З кредами проксі - має показати живий JSON:
mosquitto_sub -h <IP-сервера> -u esp32-c3 -P '<пароль з кроку 3>' \
  -t '/app/device/property/#' -v

# Публікація тими самими кредами в довільний підтопік - НЕ повинна дійти
# до підписника (ACL мовчки відкидає publish за дизайном mosquitto - без
# явної відмови на QoS0/QoS1):
mosquitto_pub -h <IP-сервера> -u esp32-c3 -P '<пароль з кроку 3>' \
  -t '/app/device/property/fake' -m '{}'
```

## Крок 9 — сторона плати (цей репозиторій)

`secrets.ini` (новий блок поруч з рештою `ecoflow_*`):

```ini
ecoflow_proxy_username = "esp32-c3"
ecoflow_proxy_password = "<пароль з кроку 3>"
```

`platformio.ini`, `[env:esp32-c3]` (або інший env без PSRAM за тим самим
принципом — прапорці лише на потрібних env, PSRAM-плати їх не бачать і
йдуть прямим TLS без змін):

```ini
-D ECOFLOW_MQTT_PROXY_HOST=\"<IP-сервера>\"
-D ECOFLOW_MQTT_PROXY_USERNAME=\"${secrets.ecoflow_proxy_username}\"
-D ECOFLOW_MQTT_PROXY_PASSWORD=\"${secrets.ecoflow_proxy_password}\"
```

Зібрати й прошити:

```sh
pio run -e esp32-c3 -t upload --upload-port <порт плати>
```

Перевірити живцем (плата й портал мають піднятись за кілька секунд):

```sh
curl -s http://<IP плати>/api/ecoflow/status | python3 -m json.tool
# connected: true, viaProxy: true, brokerHost: "<IP сервера>"
```

## Обслуговування

- **Додати новий пристрій EcoFlow.** Дописати серійник в
  `EcoflowDeviceRegistry.cpp` (`kDevices`) і відповідний рядок
  `topic /app/device/property/<SN> in` у `ecoflow-proxy.conf`, потім
  `sudo systemctl restart mosquitto`.
- **Перевипустити пароль проксі.** Кроки 3 і 5 заново з новим паролем (той
  самий username, `mosquitto_passwd -b` без `-c` — перезаписує лише запис
  цього користувача), синхронно оновити `secrets.ini` і перепрошити плату
  — стара й нова пари не співіснують, короткий downtime неминучий.
  **Ніколи не виводити пароль у відкритий термінал** (echo/cat команди) —
  передавати через файл (`$(cat file)` у команді) і одразу `shred -u`.
- **Прибрати проксі повністю.** `sudo systemctl stop mosquitto &&
  sudo rm /etc/mosquitto/conf.d/ecoflow-proxy.conf
  /etc/mosquitto/ecoflow_proxy_passwd /etc/mosquitto/ecoflow_proxy_acl &&
  sudo systemctl restart mosquitto` — і прибрати прапорці
  `ECOFLOW_MQTT_PROXY_*` з `platformio.ini` (плата без них мовчки повертається
  до прямого TLS, `EcoflowClient::Config::proxyHost == nullptr`).
- **Fallback навмисно відсутній.** Якщо сервер із проксі впав — плата просто
  не отримує оновлень EcoFlow, доки проксі не підніметься назад.
  Автоматичного переходу на прямий TLS немає (KISS, `docs/tech_debt.md`) —
  моніторинг здоров'я проксі й рішення про такий перехід — стадія 2, окреме
  рішення.

## Типові помилки

| Симптом | Причина | Фікс |
| :--- | :--- | :--- |
| `Error: Unable to open pwfile` / exit 13 | `password_file`/`acl_file` не належать `mosquitto:mosquitto` | Крок 5 |
| Плата: `disconnected, not authorised` у `mosquitto.log` | username/пароль у `secrets.ini` не збігаються з тим, що в `ecoflow_proxy_passwd` | перевірити крок 3 і крок 9 |
| `Connecting bridge` повторюється, без `running` | `remote_username`/`remote_password`/`remote_clientid` невірні або відкликані EcoFlow | звірити з `secrets.ini` (`ecoflow_mqtt_*`); типова тиха відмова описана в пам'яті проєкту "EcoFlow: відкликані ключі" |
| Підписка проходить, дані порожні | акаунт не того каналу (Open Platform замість App Private чи навпаки) | `docs/ecoflow.md`, розділ "Два канали" - схема топіка інша |
| mosquitto не стартує, порт зайнятий | інший listener/сервіс уже висить на тому ж порту | `sudo ss -ltnp \| grep 1883` |
