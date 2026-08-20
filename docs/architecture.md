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

Сім середовищ. Ідентичність плати в коді — build flag `BOARD_*` (третя колонка), саме за ним
розгалужується `src/main.cpp` і `src/Display.*`.

| env | MCU / плата | `BOARD_*` flag | Дисплей (контролер, графічна бібліотека) | Flash / PSRAM |
| :--- | :--- | :--- | :--- | :--- |
| `esp32-4848s040` | **ESP32-S3**, GUITION/Sunton ESP32-4848S040C | `BOARD_4848S040` | 480×480 RGB565, ST7701 (RGB-панель), `lovyan03/LovyanGFX@^1.1.16` | 16 MB / OPI PSRAM |
| `esp32-s3-lcd147` | **ESP32-S3**, Waveshare ESP32-S3-LCD-1.47 | `BOARD_ESP32_S3_LCD147` | 172×320 RGB565, ST7789, `bodmer/TFT_eSPI@^2.5.43` | 16 MB / OPI PSRAM |
| `esp32-st7789` | **ESP32** (esp32dev) + ST7789 SPI | `BOARD_ST7789` | 240×320 RGB565, ST7789, `bodmer/TFT_eSPI@^2.5.43` | 4 MB / — |
| `ttgo-t1` | **ESP32**, LilyGO T-Display | `BOARD_TTGO_T1` | 135×240 RGB565, ST7789, `bodmer/TFT_eSPI@^2.5.43` | 16 MB / — |
| `esp32-c6` | **ESP32-C6** (RISC-V), Waveshare ESP32-C6-LCD-1.47 | `BOARD_ESP32_C6` | 172×320 RGB565, JD9853 (командно ST7789-сумісний), `moononournation/Arduino_GFX@^1.6.0` | 8 MB / — |
| `esp32-c6-lcd096` | **ESP32-C6** (RISC-V), "ESP32-C6-LCD-0.96" | `BOARD_ESP32_C6_LCD096` | 160×80 RGB565, **ST7735S** (не ST7789, попри опис товару), `moononournation/Arduino_GFX@^1.6.0` | 4 MB / — |
| `esp8266` | **ESP8266**, NodeMCU v2 + SSD1306 OLED (I2C) | `BOARD_ESP8266` | 128×64 монохром (1 bit), SSD1306, `adafruit/Adafruit SSD1306@^2.5.13` | 4 MB / — |

Усі environments — `framework = arduino`. ESP-IDF v5.5.4 / Arduino Core 3.3.9 під капотом
Arduino-framework для ESP32-середовищ (не "чистий" ESP-IDF).

> **Про `-std`:** у `[common] build_flags` вказано `-std=gnu++17`, але фактично він **не діє** —
> arduino-esp32 додає власний `-std=gnu++2a` ПІСЛЯ наших флагів у тій самій команді, а gcc бере
> останній. Тобто проєкт реально збирається як C++20. Перевірити: `compile_commands.json`.

### Матриця фіч по платах

Джерело — `build_flags`/`lib_deps` кожного env; порожня клітинка == фіча вимкнена/відсутня.

| Фіча (як вмикається) | 4848s040 | s3-lcd147 | st7789 | ttgo-t1 | c6 | c6-lcd096 | esp8266 |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| `TFT_ROTATION` | 2 | 3 | 3 | 3 | 3 | 1 | 2 |
| `DISPLAY_SPLIT_COUNT` (смуг на кадр) | 1 | 2 | 6 | 1 | 4 | 4 | 1 |
| `SPRITE_COLOR_DEPTH` | 16 | 16 | 16 | 16 | 16 | 16 | 1 |
| SD (`BOARD_HAS_SD`) | SPI | SD_MMC 4-bit | SPI | — | — ¹ | SPI ² | — |
| Touch (`BOARD_HAS_TOUCHSCREEN`) | GT911 | — | XPT2046 | — | — | — | — |
| Light sensor (`LIGHT_SENSOR_PIN`) | — | — | GPIO34 | — | — | — | — |
| Кнопка (`FLIP_BUTTON_PIN`) | — | GPIO0 | GPIO0 | GPIO0 | — | **GPIO9** | GPIO0 |
| LED (`BLINK_LED_PIN`) | — | — | — | — | — | — | GPIO2 |
| MQTT-бекенд | PubSubClient | PubSubClient | PubSubClient | PubSubClient | **PicoMQTT** | **PicoMQTT** | PubSubClient |
| Ping (`ESPping` в `lib_deps`) | ✅ | ✅ | ✅ | ✅ | — | — | — |
| Gmail (`ESP Mail Client`) | ✅ | ✅ | — | — | ✅ | ✅ | ✅ |
| JPEG-декодер | TJpg_Decoder | TJpg_Decoder | TJpg_Decoder | TJpg_Decoder | **JPEGDEC** ³ | **JPEGDEC** ³ | TJpg_Decoder |
| Фон із LittleFS (`LITTLEFS_BACKGROUND_IMAGE`) | `/background-02-480x480.jpg` | `/background-01-320x172.jpg` | — | — | `/background-02-320x172.jpg` | `/background-01-160x80.jpg` | — |
| Вбудовані фони (`BACKGROUND_IMAGES_COUNT`) | 0 | 0 | **1** | 0 | 0 | 0 | 0 |
| Хвіст логів на екран (`SCREEN_LOG_TAIL_LINES`) | 0 | 0 | 0 | 0 | 0 | 0 | 0 |

¹ Піни SD для `esp32-c6` у `build_flags` задані, але `BOARD_HAS_SD=0` — код SD не компілюється.
² `esp32-c6-lcd096`: SPI-шина **спільна** з дисплеєм (SCK=7, MOSI=6, MISO=5), окремі CS: 4 (SD) /
  14 (LCD). `SPI.begin()` робить `setupSD()` — обов'язково **до** `setupDisplay()`.
³ RISC-V (C6) не толерантний до невирівняного доступу до пам'яті; tjpgd під TJpg_Decoder на ньому
  повертав биті значення заголовка (`width>0`, `height=0`). Тому на обох C6 — `bitbank2/JPEGDEC`.

### Розділи флеш-пам'яті

Жодна плата **не має OTA** — один розділ `app`, тому оновлення лише через USB.

| env | CSV | `app` | `nvs` | `spiffs` (LittleFS) |
| :--- | :--- | ---: | ---: | ---: |
| `esp32-4848s040` | `partitions_4848s040.csv` | 4 MB | 960 KB | 11 MB |
| `esp32-s3-lcd147` | `partitions_esp32_s3_lcd147.csv` | 3 MB | 960 KB | 11.9 MB |
| `esp32-st7789` | `partitions_st7789.csv` | 1.9 MB | 500 KB | 1.5 MB |
| `ttgo-t1` | `partitions_ttgo_t1.csv` | 3 MB | 960 KB | 11.9 MB |
| `esp32-c6` | `partitions_c6.csv` | 2 MB | 960 KB | 4.9 MB |
| `esp32-c6-lcd096` | `partitions_c6_lcd096.csv` | 2 MB | 384 KB | 1.5 MB |
| `esp8266` | — (`board_build.filesystem = littlefs`) | — | — | — |

### Платформо-специфічні теки

Кожен env підмішує рівно одну теку через `build_src_filter = +<*> +<../src-<board>/>`. Там живе
лише те, що не можна тримати спільним — насамперед визначення глобального об'єкта `tft`
(`TftInstance.cpp`) і реалізації touch-контролерів:

`src-4848s040/` (+ GT911Touch, TouchController) · `src-esp32-s3-lcd147/` · `src-st7789/`
(+ TouchController) · `src-ttgo-t1/` · `src-esp32-c6/` · `src-esp32-c6-lcd096/` · `src-esp8266/`
(+ MonoImageExample)

### Як обирається графічний бекенд

Два різні механізми, не плутати:

1. **Тип `TFT_eSPI`/`TFT_eSprite`** обирає `src/TftInstance.h` — ланцюжком `#if defined(BOARD_*)`,
   **не** через `-include`. Зроблено саме так, щоб важкий `<LovyanGFX.hpp>` тягнувся лише туди,
   де він реально потрібен, а не в кожен `.cpp` проєкту й бібліотек:

   | `BOARD_*` | Заголовок | Що всередині |
   | :--- | :--- | :--- |
   | `BOARD_4848S040` | `include/Setup_ST7701_4848S040.h` | клас LGFX + alias `TFT_eSPI` |
   | `BOARD_ESP8266` | `include/Setup_SSD1306_NodeMCU.h` | фасад над `Adafruit_SSD1306` |
   | `BOARD_ESP32_C6` | `include/Setup_JD9853_C6.h` | фасад над `Arduino_GFX` (JD9853) |
   | `BOARD_ESP32_C6_LCD096` | `include/Setup_ST7735_C6_LCD096.h` | фасад над `Arduino_GFX` (ST7735S) |
   | *(інше: st7789, ttgo-t1, s3-lcd147)* | `<TFT_eSPI.h>` | справжній bodmer/TFT_eSPI |

2. **Конфіг самого bodmer/TFT_eSPI** (піни, драйвер, шрифти) для двох плат подається через
   `-include`: `esp32-st7789` → `include/Setup_ST7789.h`, `esp32-s3-lcd147` →
   `include/Setup_ST7789_lcd147.h`. `ttgo-t1` користується конфігом самої бібліотеки.

Прикладний код (`src/Display.h`/`.cpp`) однаковий для всіх плат — він знає лише про
`TFT_eSPI`/`TFT_eSprite`-подібний інтерфейс.

## Ключові бібліотеки (спільні + платформо-специфічні)

| Бібліотека | Призначення |
| :--- | :--- |
| `lovyan03/LovyanGFX` | рендеринг для `esp32-4848s040` (RGB-панель) |
| `bodmer/TFT_eSPI` | рендеринг для `esp32-st7789`, `ttgo-t1`, `esp32-s3-lcd147` (SPI TFT) |
| `moononournation/GFX Library for Arduino` | рендеринг для обох C6-плат (JD9853, ST7735S) |
| `adafruit/Adafruit SSD1306` + `Adafruit GFX` | рендеринг для `esp8266` (I2C OLED) |
| `bodmer/TJpg_Decoder` | декодування JPEG (усі плати, крім C6) |
| `bitbank2/JPEGDEC` | декодування JPEG на C6 (RISC-V, unaligned access — див. виноску ³ вище) |
| `bblanchon/ArduinoJson` | серіалізація/конфіги |
| `tamctec/TAMC_GT911` | touch-контролер для 4848S040 |
| `PaulStoffregen/XPT2046_Touchscreen` | touch-контролер для ST7789-плати |
| `knolleary/PubSubClient` | MQTT-клієнт (5 плат) |
| `mlesniew/PicoMQTT` | MQTT-клієнт для обох C6-плат |
| `mathieucarbou/AsyncTCP` + `ESPAsyncWebServer` | вбудований HTTP-сервер (`lib/HttpServer`) |
| `mobizt/ESP Mail Client` | відправка email (`lib/GmailSender`) |
| `dvarrel/ESPping` | ping-діагностика мережі |

Власні внутрішні бібліотеки в `lib/`: `EventDispatcher`, `TaskController` (cron/job scheduler),
`ConfigStorage` (NVS-конфіг), `TouchScreen` (events/swipe/hold + point mapping),
`JpegImage`, `Pixel` (value-type для пер-піксельних обчислень), `ImageEffects`
(ефекти над буфером `JpegImage`: desaturate/lighten/darken/tint/contrast/sepia/hue-rotate/
thermal/invert/threshold/dithering/box-blur — усе in-place, без копії буфера на весь кадр),
`AnalogSensor`, `MqttClient`, `NtpService`, `HttpServer`, `SDCardInspector`,
`EspPartitionInspector`, `SystemReset`, `SerialCommander`, `Logger`, `HeapMonitor`, `RwLock`.

### MQTT topic-префікс (`MqttKeyGenerator`)

`lib/MqttClient/MqttKeyGenerator.{hpp,cpp}` — рознесення MQTT-каналів на одному брокері
(наприклад `dev`/`prod`/`qa`/`local`, регіон, тенант тощо). Формат: `{prefix}/{topic}`.

- `MqttClient::setKeyGenerator(MqttKeyGenerator*)` — pointer injection (nullptr = без
  префікса, як було раніше). Застосовується автоматично до **всіх** топіків:
  `publish`/`subscribe`/`unsubscribe`/`addListener` (і похідних `addStringListener`/
  `addJsonListener`/`addStructListener`/`addNumberListener`), а також до LWT topic
  (`connect()`/`disconnect()`).
- Топік у callback листенера — це вже фактичний (префіксований) topic з брокера, без
  автоматичного striping префікса назад.
- Джерело prefix: build-time дефолт `MqttConfig::prefix` (з `MQTT_TOPIC_PREFIX` /
  `secrets.mqtt_topic_prefix`), опційно перекривається runtime-значенням з
  `ConfigStorage` (ключ `CFG_MQTT_TOPIC_PREFIX = "mqtt.prefix"`) — override injected
  через `setKeyGenerator()` **лише якщо** значення реально збережено в NVS
  (`src/main.cpp::setupMqttClient()`). Serial-команда `mqtt-prefix [prefix]` —
  переглянути/змінити й зберегти в NVS; **зміна вимагає reboot** (топіки вже
  засабскрайблені зі старим префіксом).

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
  `BOARD_TTGO_T1`, `BOARD_ESP32_S3_LCD147`, `BOARD_ESP32_C6`, `BOARD_ESP32_C6_LCD096`,
  `BOARD_ESP8266`), а не через дублювання файлів.
- **Секрети** (WiFi, Gmail, MQTT, роутер) підвантажуються з `secrets.ini` (не в репозиторії)
  через `extra_configs` — не хардкодити креденшли в `platformio.ini` чи в коді. Доступні
  макроси: `WIFI_*`, `GMAIL_*`, `MQTT_*`, `ROUTER_HOST`, `ROUTER_LOGIN_AUTHORIZATION`.
- **`DISPLAY_SPLIT_COUNT` — це компроміс RAM проти цілісності кадру.** Спрайт виділяється не
  на весь екран, а на `height / DISPLAY_SPLIT_COUNT`, і кожна ітерація `loop()` малює лише одну
  горизонтальну смугу (`Display::startWrite()` просуває `_activeSplitBlock`, `Display::dXY()`
  зсуває координати). Плата за економію: повний кадр оновлюється за N ітерацій, тому вміст, що
  змінюється між ітераціями (секунди годинника, вільна купа), може «розриватись» на межах смуг.
  Тому там, де RAM вистачає, значення = 1.
- **Діагностика компілятора увімкнена свідомо**: `-Wall -Wextra -Wformat -Wformat-security` у
  `[common] build_flags`, без `-Werror` (сторонні бібліотеки шумлять — збірка не має від цього
  падати). `-Wundef` НЕ вмикати глобально: arduino-esp32 core рясно робить
  `#if CONFIG_IDF_TARGET_*` для незаданих макросів і повністю ховає наш власний шум.

## Форматування коду

Конфіг `.clang-format` лежить в корені репозиторію (актуальний, збігається з project
instructions):

```yaml
BasedOnStyle: Google
IndentWidth: 2
AccessModifierOffset: -2
BreakBeforeBraces: Attach
ColumnLimit: 120
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

## Тестування

Покроковий план перевірки кожної плати (що саме, якою командою, який очікуваний результат) —
у `docs/test_plan.md`. Матриця фіч вище задає, які секції плану релевантні для якого env.

## Changelog

- 2026-08-20 (2) — додано `docs/test_plan.md`: чек-лист приймальної перевірки для кожного з
  7 environments (smoke + функціональні тести + регресії після код-рев'ю), з очікуваним
  результатом на кожен крок.
- 2026-08-20 — секцію "Плати" переписано під фактичний `platformio.ini`: додано сьомий env
  `esp32-c6-lcd096` (ST7735S 160×80, ESP32-C6, 4 MB), додано матрицю фіч по платах
  (rotation / split / SD / touch / сенсор / кнопка / MQTT-бекенд / ping / gmail / JPEG-декодер /
  фони), таблицю розділів флеш-пам'яті та розділ "Як обирається графічний бекенд".
  Виправлено фактичні розбіжності з кодом:
  (а) вибір `TFT_eSPI`-типу йде через `#if defined(BOARD_*)` у `src/TftInstance.h`, а `-include`
  використовують лише 2 з 7 env (і лише для конфігу самого bodmer/TFT_eSPI);
  (б) ключ ConfigStorage — `mqtt.prefix`, а не `mqtt-prefix`;
  (в) `-std=gnu++17` з `[common]` фактично перебивається пізнішим `-std=gnu++2a` від фреймворку;
  (г) заголовок для `esp32-c6` називається `Setup_JD9853_C6.h` (у changelog 2026-08-13 він
  згаданий під робочою назвою `Setup_ArduinoGFX_C6.h`).
  Додано рішення про `DISPLAY_SPLIT_COUNT` і про набір warning-флагів.
- 2026-08-12 (3) — усі спецефекти (існуючі й нові) підключено до `SerialCommander` під
  `#if defined(LITTLEFS_BACKGROUND_IMAGE)`: `lighten`, `invert`, `threshold`, `hue`,
  `thermal`, `gamma`, `posterize`, `solarize`, `duotone`, `balance`, `noise`, `vignette`,
  `pixelate`, `scanlines`, `chromatic`, `sobel`, `emboss` (доповнюють вже наявні
  `blur`/`tint`/`contrast`/`sepia`/`desaturate`/`darken`/`dither`/`background`). Повний
  список команд і параметрів — таблиця в `docs/image_effects.md`.
- 2026-08-12 (2) — додано 12 нових ефектів: пер-піксельні `fxGamma`, `fxPosterize`,
  `fxSolarize`, `fxDuotone`, `fxColorBalance` (`Pixel.hpp`) та буфер-вайд `applyVignette`,
  `applyPixelate`, `applyScanlines`, `applyChromaticAberration`, `applySobelEdges`,
  `applyEmboss`, `applyNoise` (`ImageEffects`). `applySobelEdges`/`applyEmboss` використовують
  спільну 3-рядкову (prev/cur/next) техніку згортки 3x3 без буфера на весь кадр.
  Повний довідник усіх ефектів (існуючих і нових) з параметрами та прикладами —
  `docs/image_effects.md`.
- 2026-08-12 — `ImageEffects::applyDithering` (тільки RGB332) розбито на три окремі методи
  за глибиною кольору: `applyDitheringRGB332`, `applyDitheringRGB565`, `applyDitheringRGB888`.
  Кожен перевіряє `colorDepth()` під свою назву й повертає `false` при невідповідності.
  Спільний прохід (Bayer 8x8) винесено в приватний `applyOrderedDither(image, spread)`,
  амплітуда шуму підібрана під квантування конкретної глибини (RGB332: 0.14, RGB565: 0.03,
  RGB888: 0.004 - на RGB888 ефект практично непомітний, бо квантування вже немає, метод
  існує заради єдиного інтерфейсу команд для всіх плат).
- 2026-08-11 — додано `lib/JpegImage/Pixel.hpp` (struct-based value-type для пер-піксельних
  RGB332/RGB565/RGB888 обчислень, уніфіковані `unpack`/`pack<T>()` за типом аргументу,
  ланцюжок `fx*`-методів) та `lib/JpegImage/ImageEffects.{hpp,cpp}` (клас над `JpegImage`:
  desaturate/lighten/darken/tint/contrast/sepia/hue-rotate/thermal/invert/threshold/dithering/
  box-blur, усе in-place, без буфера на весь кадр — критично для плат без PSRAM). Ефекти
  свідомо винесені з `JpegImage` в окремий клас (різна відповідальність: декодування JPEG
  vs трансформація буфера; `SerialCommander` залежить лише від `ImageEffects`, не тягне
  JPEG-специфіку). Додано serial-команду `blur <radius> [passes]` над `spaceImage`
  (лише під `#if defined(LITTLEFS_BACKGROUND_IMAGE)`).
- 2026-08-13 — додано плату `esp32-c6` (Waveshare ESP32-C6-LCD-1.47, JD9853 172×320,
  RISC-V, без PSRAM). Дисплей JD9853 не підтримується LovyanGFX/TFT_eSPI (вже в проєкті) —
  командно сумісний з ST7789, тому підключено `moononournation/Arduino_GFX` як третю
  графічну бібліотеку лише для цієї плати, обгорнуту в TFT_eSPI/TFT_eSprite-сумісний
  фасад `include/Setup_ArduinoGFX_C6.h` (той самий підхід, що й `Setup_SSD1306_NodeMCU.h`
  для esp8266) — `src/Display.h`/`.cpp` лишились без змін. "Спрайт" — обгортка над
  `Arduino_Canvas`, `pushSprite()` реалізовано через `draw16bitRGBBitmap()` напряму
  в tft_ (Arduino_Canvas не підтримує рантайм-зсув output x/y, потрібний для
  `DISPLAY_SPLIT_COUNT`). Перший коміт: дисплей + WiFi/NTP/MQTT/SD/serial, без touch
  (AXS5106L) і IMU (QMI8658A) — окрема задача. Партиції: `partitions_c6.csv` (8MB,
  без OTA). Піни, TFT_ROTATION, offset'и JD9853 і DISPLAY_SPLIT_COUNT потребують
  валідації на реальному пристрої.
- 2026-08-09 — таблиця плат: додано колонку платформи (ESP32/ESP32-S3/ESP8266) в
  "Плата", деталізовано "Дисплей" (width×height, color depth, controller, driver@version),
  додано рядок `esp32-s3-lcd147`.
- 2026-08-09 — додано `MqttKeyGenerator` (`lib/MqttClient/`): topic-префікс (dev/prod/qa/
  local, регіон, тощо) для всіх MQTT-топіків, injected в `MqttClient` через
  `setKeyGenerator()` (fallback — `MqttConfig::prefix`, build-time). Новий build flag
  `MQTT_TOPIC_PREFIX` (з `secrets.ini`, `secrets.mqtt_topic_prefix`), ConfigStorage-override
  через ключ `mqtt-prefix` і serial-команду `mqtt-prefix [prefix]` (потребує reboot).
- 2026-08-04 — файл повністю переписано на основі актуального `platformio.ini` та коду
  репозиторію (`nick-lavrik/esp32`, гілка `master`). Попередня версія описувала проєкт
  з Arduino_GFX_Library/Open-Meteo погодою — це не відповідає поточному стану коду і
  видалено з опису функціоналу. Додано розділ "Форматування коду".
