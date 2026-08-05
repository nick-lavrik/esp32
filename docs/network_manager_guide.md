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
id=Universal-Profile-Example
uuid=12345678-abcd-efgh-ijkl-1234567890ab
type=wifi
interface-name=wlan0
autoconnect=true

# =====================================================================
# СЕКЦІЯ 1: Якщо type=wifi
# =====================================================================
[wifi]
mode=infrastructure
ssid=MyHomeWiFi

[wifi-security]
key-mgmt=wpa-psk
psk=YourWiFiPassword

# =====================================================================
# СЕКЦІЯ 2: Якщо type=ethernet
# =====================================================================
[ethernet]
mac-address=00:11:22:33:44:55
auto-negotiate=true

# =====================================================================
# СЕКЦІЯ 3: Якщо type=vpn
# =====================================================================
[vpn]
service-type=org.freedesktop.NetworkManager.openvpn
connection-type=tls
remote=vpn.example.com:1194
username=my_vpn_user

[vpn-secrets]
password=MySecretVpnPassword

# =====================================================================
# СПІЛЬНІ МЕРЕЖЕВІ БЛОКИ (Потрібні для всіх типів підключень)
# =====================================================================
[ipv4]
method=manual
address1=192.168.1.150/24
gateway=192.168.1.1
dns=8.8.8.8;1.1.1.1;
route-metric=50

[ipv6]
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
