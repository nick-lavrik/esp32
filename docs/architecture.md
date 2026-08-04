# ESP32 Project — Архітектурні рішення

> Файл для knowledge base проєкту. Тут — рішення "чому саме так" і поточна конфігурація,
> стиль коду описаний окремо в project instructions і в секції "Форматування коду" нижче.
> При зміні рішень — онови відповідну секцію і додай запис у Changelog внизу.
>
> Джерела: `platformio.ini`, репозиторій https://github.com/nick-lavrik/esp32/tree/master/

## Огляд проєкту

Це не окремий "клок+погода" скетч, а спільна кодова база (`src/main.cpp`, `lib/*`) для
**кількох різних плат ESP32/ESP8266**, кожна зі своїм середовищем PlatformIO і своєю
`src-<board>/`-теку з платформо-специфічними файлами (дисплей, touch). Загальна логіка
(WiFi, NTP, ping, MQTT, HTTP-сервер, config storage, task scheduler, event dispatcher,
serial commander, light sensor, gmail sender) — спільна для всіх плат і розгалужується
через `#if BOARD_HAS_*` / `#if defined(BOARD_*)` фічефлаги.

## Плати (PlatformIO environments)

| env | Плата | Дисплей | Особливості |
|---|---|---|---|
| `esp32-4848s040` | GUITION/Sunton ESP32-4848S040C (ESP32-S3) | RGB-панель ST7701, 480×480, LovyanGFX | touch (GT911), SD, PSRAM |
| `esp32-st7789` | ESP32 Dev Module + ST7789 SPI | 240×320, `bodmer/TFT_eSPI` | touch (XPT2046), SD, light sensor |
| `ttgo-t1` | LilyGO T-Display (класична, ESP32) | 135×240, `bodmer/TFT_eSPI` | без SD, без touch |
| `esp8266` | NodeMCU ESP8266 + SSD1306 OLED | 128×64, монохром, `Adafruit SSD1306` | окрема `src-esp8266/`, без SD/touch |

Усі environments — `framework = arduino`, `-std=gnu++17`. ESP-IDF v5.5.4 / Arduino Core 3.3.9
під капотом Arduino-framework для ESP32-S3/ESP32 environments (не "чистий" ESP-IDF).

## Ключові бібліотеки (спільні + платформо-специфічні)

| Бібліотека | Призначення |
|---|---|
| `lovyan03/LovyanGFX` | рендеринг для `esp32-4848s040` (RGB-панель) |
| `bodmer/TFT_eSPI` | рендеринг для `esp32-st7789` та `ttgo-t1` (SPI TFT) |
| `adafruit/Adafruit SSD1306` + `Adafruit GFX` | рендеринг для `esp8266` (I2C OLED) |
| `bodmer/TJpg_Decoder` | декодування JPEG (фонові зображення) |
| `bblanchon/ArduinoJson` | серіалізація/конфіги |
| `tamctec/TAMC_GT911` | touch-контролер для 4848S040 |
| `PaulStoffregen/XPT2046_Touchscreen` | touch-контролер для ST7789-плати |
| `knolleary/PubSubClient` | MQTT-клієнт |
| `mathieucarbou/AsyncTCP` + `ESPAsyncWebServer` | вбудований HTTP-сервер (`lib/HttpServer`) |
| `mobizt/ESP Mail Client` | відправка email (`lib/GmailSender`) |
| `dvarrel/ESPping` | ping-діагностика мережі |

Власні внутрішні бібліотеки в `lib/`: `EventDispatcher`, `TaskController` (cron/job scheduler),
`ConfigStorage` (NVS-конфіг), `TouchScreen` (events/swipe/hold + point mapping),
`JpegImage`, `AnalogSensor`, `MqttClient`, `NtpService`, `HttpServer`, `SDCardInspector`,
`EspPartitionInspector`, `SystemReset`, `SerialCommander`, `Logger`, `HeapMonitor`.

## Функціонал (актуальний, за `src/main.cpp`)

- Годинник (`drawTime`, вмикається/вимикається через конфіг `CFG_SHOW_CLOCK`)
- Системна інфо-панель на екрані: uptime, CPU freq, loop rate, вільна память, ping-статистика,
  яскравість
- Фонові зображення — циклічний показ (`BackgroundImages`) із заданим інтервалом (5 сек),
  або активне JPEG-зображення з LittleFS (`setupBackgroundImage`), якщо завантажено
- Touch-жести (swipe у 4 напрямках + з країв екрана, hold, double-click) — керування
  яскравістю, flip екрана, малювання debug-рамки
- Light sensor → авто-яскравість (опціонально, через конфіг)
- MQTT-клієнт з LWT (last will and testament)
- Вбудований HTTP-сервер, що роздає статику з LittleFS
- SerialCommander — керування пристроєм через serial-консоль (`list` для команд)
- ConfigStorage — персистентні налаштування (показ годинника, авто-яскравість, яскравість)
- **Погодного функціоналу (Open-Meteo) в поточному коді немає** — це не реалізовано
  (якщо планується — потрібно додавати як нову фічу, а не "відновлювати")

## Рішення, яких варто триматись

- **LVGL не використовується** (закоментовано в `platformio.ini`). Причина, з якої це
  колись відхилялось: конфлікти компіляції LVGL з FATFS-драйвером на платі 4848S040.
  Рендеринг — напряму через LovyanGFX/TFT_eSPI. Не пропонувати LVGL повторно без нової
  вагомої причини.
- **SD-картка опціональна** (`BOARD_HAS_SD`) — код має коректно працювати і без SD
  (fallback-логіка обов'язкова для будь-якого функціоналу, що читає з SD).
- **Платформо-специфічний код виноситься в `src-<board>/`**, спільна логіка лишається
  в `src/` і `lib/`, розгалуження — через build flags (`BOARD_4848S040`, `BOARD_ST7789`,
  `BOARD_ESP8266`, `BOARD_TTGO_T1`), а не через дублювання файлів.
- **Секрети** (WiFi, Gmail, MQTT) підвантажуються з `secrets.ini` (не в репозиторії) через
  `extra_configs` — не хардкодити креденшли в `platformio.ini` чи в коді.

## Форматування коду

Конфіг `.clang-format` лежить в корені репозиторію (актуальний, збігається з project
instructions):

```yaml
BasedOnStyle: Google
IndentWidth: 2
AccessModifierOffset: -2
BreakBeforeBraces: Attach
ColumnLimit: 100
```

Плюс правила іменування (з project instructions, clang-format це не покриває):
- приватні змінні-члени класу — префікс `_`
- імена методів/членів — `camelCase`
- ім'я класу/структури — `PascalCase`
- ім'я файлу == ім'я класу/структури, який у ньому описаний (`TouchEvents.h`/`.cpp` для
  `class TouchEvents`)
- один клас/структура — один файл (для малих класів достатньо одного `.hpp`)
- на початку `.h`/`.hpp` — короткий сніппет ініціалізації/використання

Перед комітом бажано прогнати `clang-format -i` на змінених `.cpp`/`.h`/`.hpp` файлах.

## Changelog

- 2026-08-04 — файл повністю переписано на основі актуального `platformio.ini` та коду
  репозиторію (`nick-lavrik/esp32`, гілка `master`). Попередня версія описувала проєкт
  з Arduino_GFX_Library/Open-Meteo погодою — це не відповідає поточному стану коду і
  видалено з опису функціоналу. Додано розділ "Форматування коду".
