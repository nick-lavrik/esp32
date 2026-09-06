# Довідник з налаштування та керування NetworkManager

Цей документ містить опис структури конфігураційних файлів, базові та розширені команди утиліти `nmcli`, методи імпорту мереж, а також інструкції з діагностики та розгортання точок доступу.

---

## Загальна інформація

* **Розташування файлів:** Усі профілі з'єднань зберігаються в каталозі `/etc/NetworkManager/system-connections/`.
* **Назва файлу:** Зазвичай збігається з назвою мережі та має розширення `.nmconnection` або взагалі без нього.
* **Права доступу:** Тільки користувач `root` повинен мати доступ до цих файлів. Безпечний режим дозволів — `600` (`chmod 600 <файл>`). Якщо права виставлені неправильно, NetworkManager проігнорує конфігурацію з міркувань безпеки.
* **Пріоритет мереж (Route Metric):** Linux обирає маршрут за найменшим індексом метрики. Провідний Ethernet має пріоритет за замовчуванням (метрика `100`), тоді як Wi-Fi є резервним (метрика `600`).
* **Гаряче перезавантаження:** Будь-які ручні зміни у текстових файлах не застосовуються «на льоту». Потрібно явно наказати демону перечитати файли конфігурації.

---

## Універсальний макет конфігураційного файлу

Файли NetworkManager побудовані за модульним принципом: вони мають спільні блоки (`[connection]`, `[ipv4]`, `[ipv6]`) та окремі специфічні секції, які вмикаються залежно від значення параметра `type`.

Нижче наведено структуру, яка демонструє одночасно всі популярні типи підключень. На практиці в одному файлі ви залишаєте блок `[connection]`, блоки IP-налаштувань та **лише одну** із секцій типу мережі (`[wifi]`, `[ethernet]` або `[vpn]`).

```ini
[connection]
# [ОБОВ'ЯЗКОВО] Назва підключення у системі
id=Universal-Profile-Example
# [ОБОВ'ЯЗКОВО] Унікальний ідентифікатор профілю (UUIDv4)
uuid=12345678-abcd-efgh-ijkl-1234567890ab
# [ОБОВ'ЯЗКОВО] Тип з'єднання.
# Можливі значення: 
#   - wifi (бездротова мережа)
#   - ethernet (дротовий інтернет/LAN)
#   - vpn (віртуальна приватна мережа)
#   - gsm / cdma (мобільний 4G/5G інтернет через модем)
#   - bridge / bond / team (віртуальні агреговані інтерфейси)
#   - bluetooth (роздача інтернету з телефону по BT)
#   - tun / tap (мережеві тунелі)
#   - vlan (віртуальні локальні мережі)
type=wifi
# [ОПЦІОНАЛЬНО] Фізичний інтерфейс (наприклад: eth0, wlan0, usb0). Якщо прибрати, профіль застосується до будь-якої сумісної плати.
interface-name=wlan0
# [ОПЦІОНАЛЬНО] Автоматичне підключення при старті системи (true або false)
autoconnect=true

# =====================================================================
# СЕКЦІЯ 1: Якщо type=wifi (Для Ethernet та VPN блоки нижче видаляються)
# =====================================================================
[wifi]
# [ОБОВ'ЯЗКОВО для Wi-Fi] Режим роботи: infrastructure (клієнт), ap (роздача/гаряча точка), mesh, adhoc
mode=infrastructure
# [ОБОВ'ЯЗКОВО для Wi-Fi] Назва бездротової мережі (SSID)
ssid=MyHomeWiFi

[wifi-security]
# [ОБОВ'ЯЗКОВО для закритих Wi-Fi] Тип автентифікації. 
# Значення: wpa-psk (WPA2), sae (WPA3-Personal), wpa-eap (Enterprise), none (відкрита мережа)
key-mgmt=wpa-psk
# [ОБОВ'ЯЗКОВО в цій секції] Пароль до Wi-Fi мережі
psk=YourWiFiPassword

# =====================================================================
# СЕКЦІЯ 2: Якщо type=ethernet (Для Wi-Fi та VPN блоки вище/нижче видаляються)
# =====================================================================
[ethernet]
# [ОПЦІОНАЛЬНО] Прив'язка профілю до конкретної мережевої карти за її фізичною MAC-адресою
mac-address=00:11:22:33:44:55
# [ОПЦІОНАЛЬНО] Швидкість порту в Мбіт/с (зазвичай визначається авто, але можна зафіксувати, наприклад: 10, 100, 1000)
auto-negotiate=true

# =====================================================================
# СЕКЦІЯ 3: Якщо type=vpn (Для Wi-Fi та Ethernet блоки вище видаляються)
# =====================================================================
[vpn]
# [ОБОВ'ЯЗКОВО для VPN] Назва VPN-плагіна (наприклад: openvpn, wireguard, l2tp, pptp, strongswan)
service-type=org.freedesktop.NetworkManager.openvpn
# [ОПЦІОНАЛЬНО] Специфічні налаштування VPN-провайдера (параметри залежать від типу service-type)
connection-type=tls
remote=vpn.example.com:1194
username=my_vpn_user

[vpn-secrets]
# [ОПЦІОНАЛЬНО] Паролі або закриті ключі для авторизації у VPN
password=MySecretVpnPassword

# =====================================================================
# СПІЛЬНІ МЕРЕЖЕВІ БЛОКИ (Потрібні для всіх типів підключень)
# =====================================================================
[ipv4]
# [ОБОВ'ЯЗКОВО] Метод конфігурації IP.
# Значення: 
#   - auto (отримання IP від роутера через DHCP)
#   - manual (статичний IP, який прописується вручну нижче)
#   - link-local (автоматичний IP без роутера у діапазоні 169.254.x.x)
#   - shared (роздача інтернету іншим пристроям, ПК стає роутером)
#   - disabled (повністю вимкнути IPv4 для цього інтерфейсу)
method=manual
# [ОБОВ'ЯЗКОВО лише для method=manual] Статичний IP та маска підмережі у форматі CIDR (/24 = 255.255.255.0)
address1=192.168.1.150/24
# [ОПЦІОНАЛЬНО для method=manual] Основний шлюз для виходу в інтернет (IP вашого роутера)
gateway=192.168.1.1
# [ОПЦІОНАЛЬНО] Список IP-адрес DNS-серверів. Обов'язково розділяються крапкою з комою `;`
dns=8.8.8.8;1.1.1.1;
# [ОПЦІОНАЛЬНО] Метрика маршруту (пріоритет). Менше число = вища важливість мережі.
route-metric=50

[ipv6]
# [ОБОВ'ЯЗКОВО] Метод конфігурації IPv6.
# Значення: auto, manual, link-local, disabled, ignore (ігнорувати помилки відсутності IPv6)
method=ignore
```

---

## Навіщо потрібен nmcli та корисні команди

Утиліта `nmcli` — це офіційний CLI-інтерфейс для керування NetworkManager. На відміну від прямого редагування текстових файлів, `nmcli` **діє миттєво, автоматично валідує синтаксис**, генерує правильні UUID та відразу записує конфігурацію на диск.

Головні команди для роботи з конфігураційними файлами:

1. **Перечитати конфігураційні файли з диска:**
   ```bash
   sudo nmcli connection reload
   ```
2. **Перезапустити конкретний інтерфейс (застосувати зміни):**
   ```bash
   sudo nmcli connection down "Universal-Profile-Example" && sudo nmcli connection up "Universal-Profile-Example"
   ```
3. **Створити нове підключення з ручним IP через CLI:**
   ```bash
   sudo nmcli connection add type ethernet con-name "StaticWired" ifname eth0 ipv4.method manual ipv4.addresses 192.168.1.100/24 ipv4.gateway 192.168.1.1 ipv4.dns "8.8.8.8,1.1.1.1"
   ```
4. **Змінити метрику (пріоритет) мережі на льоту:**
   ```bash
   sudo nmcli connection modify "Universal-Profile-Example" ipv4.route-metric 45
   ```

---

## Імпорт конфігурацій з інших джерел

NetworkManager підтримує імпорт готових профілів у різних популярних форматах.

### 1. Імпорт конфігурацій VPN (на прикладі .ovpn)
Для імпорту файлів OpenVPN у системі має бути встановлений плагін `NetworkManager-openvpn`:
```bash
sudo nmcli connection import type openvpn file /шлях/до/файлу.ovpn
```
*Для імпорту файлів WireGuard (`.conf`):*
```bash
sudo nmcli connection import type wireguard file /шлях/до/wg0.conf
```

### 2. Імпорт Wi-Fi мереж за допомогою QR-кодів
Якщо у вас є QR-код Wi-Fi мережі, його можна розпізнати утилітою `zbarimg`:
```bash
sudo apt install zbar-tools
WIFI_DATA=\$(zbarimg --raw /шлях/до/qr_code.png)
SSID=(echo "WIFI_DATA" | sed -n 's/.*S:\([^;]*\);.*/\1/p')
PASSWORD=(echo "WIFI_DATA" | sed -n 's/.*P:\([^;]*\);.*/\1/p')

sudo nmcli device wifi connect "SSID" password "PASSWORD"
```

### 3. Імпорт за допомогою WPS
* **Через фізичну кнопку (PBC):**
  ```bash
  sudo nmcli device wifi wps wlan0 mode pbc
  ```
* **Через PIN-код роутера:**
  ```bash
  sudo nmcli device wifi wps wlan0 mode pin pin 12345678
  ```

---

## Налаштування точки доступу (Wi-Fi Hotspot)

### 1. Швидке створення точки доступу через CLI
```bash
sudo nmcli device wifi hotspot ifname wlan0 ssid MyLinuxHotspot password "MySecurePassword123"
```

### 2. Як це виглядає всередині конфігураційного файлу
```ini
[connection]
id=Hotspot
type=wifi
interface-name=wlan0

[wifi]
mode=ap
ssid=MyLinuxHotspot

[wifi-security]
key-mgmt=wpa-psk
psk=MySecurePassword123

[ipv4]
method=shared

[ipv6]
method=shared
```

---

## Діагностика помилок (Troubleshooting)

### 1. Перевірка статусів інтерфейсів
```bash
nmcli device status
nmcli general status
```

### 2. Перегляд системних логів у реальному часі
```bash
sudo journalctl -u NetworkManager -f
```

### 3. Що робити, якщо інтерфейс у стані "unmanaged"
Перевірте файл `/etc/NetworkManager/NetworkManager.conf`:
```ini
[main]
plugins=ifupdown,keyfile

[ifupdown]
managed=true
```
*Після зміни параметра перезапустіть службу:*
```bash
sudo systemctl restart NetworkManager
```

### 4. Повне увімкнення/вимкнення радіомодулів
* Перевірити стан радіомодулів: `nmcli radio`
* Увімкнути Wi-Fi: `nmcli radio wifi on`

---

## Команда `net` на пристрої: те саме, але в ESP32

Прошивка має власний менеджер мереж (`lib/NetworkSupervisor`) і серійну команду
`net`, яка свідомо дзеркалить `nmcli` — щоб не тримати в голові дві різні
граматики. Структура та сама: `net ОБ'ЄКТ ДІЯ [аргументи]`, слова скорочуються
до унікального префікса (`net c s` == `net connection show`), імена налаштувань
у `modify` — nmcli-івські.

```
net                                       = net general status
net help | net <object> help

net general status                        стан, IP, шлюз, DNS, сигнал
net radio wifi [on|off]                   увімкнути/вимкнути менеджер

net device status                         стан інтерфейсу, MAC
net device wifi list                      скан ефіру (таблиця IN-USE/SSID/CHAN/SIGNAL)
net device wifi connect <ssid> [password <p>]
net device wifi hotspot [ssid <s>] [password <p>]
net device disconnect

net connection show [<id|ssid>]
net connection add ssid <s> [password <p>] [priority <n>]
net connection modify <id|ssid> <setting> <value>
net connection delete <id|ssid>
net connection up <id|ssid>
net connection down
net connection reload                     перечитати /network/*.nmconnection
net connection load <file>                імпортувати один файл
net connection export [<id|ssid>]         вивантажити профілі у файли
```

Налаштування для `modify` — і повні, і скорочені посекційно
(`wifi-sec.psk`, `con.autoconnect-priority`, просто `psk`):

| nmcli setting | що робить |
|---|---|
| `wifi.ssid` | SSID профілю |
| `wifi-security.psk` | пароль |
| `connection.autoconnect` `yes\|no` | чи брати профіль до уваги при підборі |
| `connection.autoconnect-priority` | пріоритет (більше = раніше) |
| `connection.autoconnect-retries` | спроб на цей профіль; `-1` = глобальний дефолт |
| `ipv4.method` `auto\|manual` | DHCP чи статика |
| `ipv4.addresses` | `192.168.1.50/24` — адреса з маскою, вмикає статику |
| `ipv4.gateway` | шлюз |
| `ipv4.dns` | DNS-сервер |

### Чим відрізняється від справжнього nmcli

* **Профіль адресується `id` або `SSID`.** UUID і `con-name` тут немає: пристрій
  тримає плаский список, а не каталог `.nmconnection`-файлів.
* **Зміни зберігаються одразу.** `add` / `modify` / `delete` самі пишуть у NVS,
  окремого `save` немає. `net connection reload` — це відкат до збереженого,
  аналог `nmcli connection reload`.
* **`radio wifi off` зупиняє менеджер цілком** (`NetworkSupervisor::end()`), а не
  лише глушить радіо через rfkill.
* **`hotspot` не гасне заради сканування.** Коли жодна зі збережених мереж не
  видима, пристрій піднімає точку доступу `ESP-<env>` і далі перевіряє ефір у
  режимі AP_STA кожні `scanIntervalMs`. Точка зникає лише тоді, коли відома
  мережа реально з'явилась. У Linux-NetworkManager такої поведінки немає — там
  hotspot це окремий профіль, який треба гасити вручну.
* **Немає `type`, `ifname`, VPN, bridge/bond/vlan** — інтерфейс рівно один,
  `wlan0`.
* **Перший старт засівається з `secrets.ini`.** Якщо список профілів у NVS
  порожній, туди додається мережа з build-flag'ів `WIFI_SSID`/`WIFI_PASSWORD`,
  щоб плата після чистої прошивки не лишилась без зв'язку. Далі build-flag
  нічого не перевизначає.

### Дві пастки arduino-esp32, на які тут є обхід

1. **`WiFi.config(INADDR_NONE, ...)` обнуляє DNS.** Гілка DHCP-клієнта в
   `NetworkInterface::config()` спершу зупиняє `dhcpc`, записує всі три
   DNS-сервери нулями і лише тоді стартує `dhcpc` назад — а той переукладає
   оренду з кешу й DNS уже не проставляє. Назовні це «IP є, LAN пінгується, а
   `hostByName()` падає з -54». Тому `_applyIpConfig()` чіпає `WiFi.config()`
   лише коли реально треба зняти раніше виставлену статику, а
   `_ensureDnsAfterDhcp()` після кожного DHCP-підключення підставляє шлюз, якщо
   DNS усе одно лишився порожнім.
2. **Не тримати мʼютекс під час викликів `WiFi.*`.** `WiFi.mode()`, `softAP()`,
   `begin()` всередині чекають на arduino event task. Якщо той упреться в той
   самий замок, стає весь WiFi-стек: команда мовчить, плата лишається на старій
   точці. Замок у `NetworkSupervisor` захищає рівно вектор профілів.

### Де живе конфігурація: NVS чи LittleFS

Сховищ два, і в них різні ролі — плутати їх дорого.

| | NVS | LittleFS `/network/*.nmconnection` |
|---|---|---|
| роль | **робоче сховище** | джерело **постачання** |
| хто пише | команди `net`, сам FSM | ви, з компа |
| переживає `pio run -t upload` | так | так |
| переживає `pio run -t uploadfs` | **так** | **ні, розділ перезаписується цілком** |
| читається людиною | ні (JSON у NVS) | так, звичайний INI |

Вирішальний рядок — передостанній. `uploadfs` пише образ файлової системи
цілком, тож усе, що додали на пристрої командою `net connection add`, у файлах
би загинуло під час найближчого деплою статики. Тому робочим сховищем лишається
NVS, а файли — це «те, що ви поклали з компа».

Звідси й правила імпорту:

* **на старті** файли читаються, лише якщо список профілів у NVS **порожній**
  (чиста плата або стерта NVS). Порядок джерел:
  `NVS → /network/*.nmconnection → WIFI_SSID/WIFI_PASSWORD з secrets.ini`;
* **будь-коли вручну** — `net connection reload` (усі файли) або
  `net connection load <file>` (один). Мерж іде за SSID: наявний профіль
  оновлюється на місці, зберігаючи `id` та історію підключень, новий —
  додається;
* `net connection delete` прибирає профіль **лише з NVS**, файл лишається
  (на відміну від справжнього nmcli, який видаляє і файл). Тобто після
  `delete` + `reload` профіль повернеться — це навмисно: файли є деклараціями
  постачання, а не дзеркалом runtime-стану;
* `net connection export` робить зворотну дію — пише поточні профілі у файли,
  щоб їх можна було зняти з плати, поправити на компі й задеплоїти назад.

### Як спорядити плату мережами з компа

```bash
mkdir -p data/network
$EDITOR data/network/home.nmconnection     # формат нижче
pio run -e esp32-c6 -t uploadfs            # УВАГА: перезаписує весь LittleFS
```

Файл (це рівно те, що лежить у `/etc/NetworkManager/system-connections/`, тож
робочий профіль можна просто скопіювати з ноутбука):

```ini
[connection]
id=HomeWiFi
type=wifi
autoconnect=true
autoconnect-priority=10
autoconnect-retries=-1

[wifi]
mode=infrastructure
ssid=HomeWiFi

[wifi-security]
key-mgmt=wpa-psk
psk=secret

[ipv4]
method=manual
address1=192.168.1.50/24,192.168.1.1
dns=8.8.8.8;1.1.1.1;

[ipv6]
method=ignore
```

Ігноруються (читаються без помилки, але не використовуються): `uuid`,
`interface-name`, `permissions`, `802-1x`/WPA-Enterprise, `ipv6` крім `method`,
другий і подальші DNS зі списку. `method=auto` в `[ipv4]` — звичайний DHCP.

Дві деталі, які легко проґавити:

* **Ім'я файлу косметичне.** Мережу визначає `wifi.ssid` усередині, як і в
  NetworkManager. Так зроблено навмисно: SSID буває до 32 символів, а LittleFS
  на ESP8266 обмежує довжину компонента шляху — прив'язка до імені файлу ламала
  б довгі SSID.
* **SSID із пробілом на краю пишеться байтами.** `ssid=65;115;117;115;32;` —
  це `"Asus "`, десяткові байти через `;`, точно як у NetworkManager. Без цієї
  форми пробіл гине при читанні, профіль перестає збігатися зі збереженим і
  кожен `reload` плодить дубль. Запис сам обирає форму, читання розпізнає обидві.

> **Пароль лежить у відкритому вигляді** — і у файлі, і в NVS (як і в
> `/etc/NetworkManager/system-connections/`, де файл рятують лише права `600`).
> На LittleFS прав немає. Якщо колись увімкнете роздачу статики з LittleFS
> (`httpServer.setStaticSource(&littleFsSource)` у `src/main.cpp` зараз
> закоментовано), `/network/*.nmconnection` стануть доступними по HTTP — тоді
> каталог треба виключити зі статики або тримати профілі лише в NVS.
