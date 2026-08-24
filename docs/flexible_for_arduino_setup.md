# Плагін «Flexible For Arduino» для JetBrains IDE — результати дослідження

Цей документ фіксує результати аудиту плагіна `Flexible For Arduino` в IntelliJ IDEA та стану його конфігурації в цьому проєкті. Саме налаштування **відкладене**, тому документ написаний як довідка «на момент повернення»: зафіксовані версії, шляхи, декомпільовані дефолти, знайдені небезпечні місця та чекліст дій.

Дані отримані розбором `flexible-arduino-2026.1.18.jar` (`META-INF/plugin.xml`, `messages/ArduinoBundle.properties`, байткод `ArduinoSettingsState` і `PlatformIoBackend`) та аудитом `.idea/` цього проєкту.

---

## 1. Статус: чому відкладено

Плагін встановлений і **частково** налаштований для цього проєкту (`activeBackend=platformio`). Довести конфігурацію до кінця не вийде «наполовину»: значна частина налаштувань і перевірок вимагає операцій **flash / serial monitor / reset пристрою**, а плата (ESP32-S3-DevKitC-1-N8, `/dev/ttyACM0`) зараз у роботі — будь-яка з цих операцій зашкодить.

Тому налаштування переноситься на момент, коли плата звільниться. `.ino`-сценарій (скетчі через `arduino-cli`) виведений зі скоупу — у роботі не задіяний.

---

## 2. Оточення: зафіксовані версії та шляхи

| Компонент | Версія | Шлях |
| :--- | :--- | :--- |
| Плагін `com.flexible.arduino` | 2026.1.18 | `~/.local/share/JetBrains/IntelliJIdea2026.2/flexible-arduino/` |
| IntelliJ IDEA | **2026.2** | плагін встановлений **тільки тут** |
| C/C++ плагін `com.intellij.clion` («Native Build Tools») | 262.9437.185 | увімкнений (немає в `disabled_plugins.txt`) |
| `arduino-cli` | 1.5.1 | `~/.local/bin/arduino-cli` |
| PlatformIO Core | 6.1.19 | `~/.platformio/penv/bin/pio` |
| Python (PIO venv) | — | `~/.platformio/penv/bin/python` |
| ESP-IDF | v6.0.2 | `~/.espressif/v6.0.2` |

Встановлені ядра `arduino-cli`: `esp32:esp32 3.3.11`, `arduino:avr 1.8.8`.

**Важливо про версію IDE:** у `~/.local/share/JetBrains/IntelliJIdea2026.1/` плагіна **немає**. Якщо проєкт відкрити в 2026.1, жодна Arduino-функціональність не працюватиме. Мінімальна підтримувана версія за `plugin.xml` — `since-build="233"` (2023.3+).

**Де живуть налаштування:** плагін використовує `projectService` + `PersistentStateComponent` з анотацією `@State(name="ArduinoSettingsState", storages=@Storage("arduino-settings.xml"))`. Тобто конфігурація — **на рівні проєкту**, у `.idea/arduino-settings.xml`, і для кожного проєкту задається окремо. Глобальних налаштувань немає.

Поточний вміст `.idea/arduino-settings.xml` цього проєкту:

```xml
<component name="ArduinoSettingsState">
  <option name="espIdfPath" value="$USER_HOME$/.espressif/v6.0.2/esp-idf" />
  <option name="pythonPath" value="$USER_HOME$/.platformio/penv/bin/python" />
  <option name="arduinoCliPath" value="$USER_HOME$/.local/bin/arduino-cli" />
  <option name="comPort" value="ttyACM1" />
  <option name="serialPort" value="ttyACM1" />
  <option name="activeBackend" value="platformio" />
  <option name="platformioPath" value="$USER_HOME$/.platformio/penv/bin/pio" />
</component>
```

Також є `.idea/arduino-subprojects.xml` з `activeSubprojectPath = "."`.

---

## 3. Небезпечні місця — перше, що зробити при поверненні

### 3.1. Run configuration названий «Build», але заливає прошивку

У `.idea/workspace.xml`:

```xml
<configuration name="Arduino Build" type="ARDUINO_RUN" factoryName="ARDUINO_RUN_FACTORY">
  <options mode="BUILD_FLASH_MONITOR" buildTarget="esp32dev" additionalArgs=""
           cleanFirst="false" port="" baudRate="921600"
           subprojectPath="" sketchFolder="" programmer="" />
</configuration>
```

Назва каже «Build», режим — `BUILD_FLASH_MONITOR`. Тобто збірка **плюс заливка плюс відкриття монітора**. Enum `ArduinoRunConfiguration$RunMode` містить окремий безпечний режим:

`BUILD` · `FLASH` · `UPLOAD_ONLY` · `MONITOR` · `BUILD_AND_FLASH` · `BUILD_FLASH_MONITOR` · `DEBUG`

**Дія:** Run → Edit Configurations → `Arduino Build` → Mode = `Build`. Або правкою `mode="BUILD"` у `workspace.xml` при закритій IDE (інакше перезапише).

### 3.2. Дефолти монітора ресетять ESP32

З декомпіляції конструктора `ArduinoSettingsState`:

| Поле | Дефолт | Наслідок |
| :--- | :--- | :--- |
| `monitorResetOnConnect` | `true` | **ресет пристрою при підключенні монітора** |
| `monitorDtrOnConnect` | `true` | DTR на ESP32 = ресет |
| `monitorRtsOnConnect` | `false` | — |
| `monitorAutoReconnect` | `true` | **сам відкриває порт** |
| `usbAutoDetect` | `true` | автопідхват порту |
| `monitorBootLoopDetection` | `true` | читає порт |
| `mcpEnabled` | `false` | — |
| `mcpPort` | `3725` | — |
| `saveBeforeBuild` | `true` | безпечно |
| `flashBaudRate` | `460800` | — |
| `baudRate` / `serialBaudRate` | `115200` | — |
| `boardTarget` | `esp32` | лише Pin Viewer |
| `flashMode` | `dio` | — |
| `flashSize` | `4MB` | — |
| `uploadMethod` | `UART` | варіанти: `UART` / `JTAG` / `DFU` |
| `lineEnding` | `CRLF` | — |
| `monitorEncoding` | `UTF-8` | — |
| `monitorTimestampFormat` | `HH:mm:ss.SSS` | — |
| `activeBackend` | `auto` | — |
| `otaPartitionScheme` | `default` | — |

### 3.3. Пастка «порожнього XML»

IntelliJ `XmlSerializerUtil` серіалізує **лише значення, відмінні від дефолтних**. Тому:

> **Відсутність поля в `arduino-settings.xml` означає «увімкнено за дефолтом», а не «вимкнено».**

Тобто зараз `monitorResetOnConnect`, `monitorDtrOnConnect`, `monitorAutoReconnect` і `usbAutoDetect` **активні**, хоча в XML їх немає. Після явного вимкнення вони з'являться у файлі як `value="false"`.

### 3.4. Що врятувало досі

У конфігу `comPort` / `serialPort` = `ttyACM1`, а плата — на `ttyACM0`. Порт неіснуючий, тому плагін нічого не відкривав. **Це єдиний фактичний стоп-кран на цей момент.** Не ставити реальний порт у налаштування, поки не вимкнені опції з 3.2.

### 3.5. MCP-сервер плагіна

Плагін піднімає власний HTTP-сервер (`com.sun.net.httpserver`) з ендпоінтами `/tools/list`, `/tools/call`, `/status` на `localhost:3725`. За дефолтом вимкнений (`mcpEnabled=false`) — так і лишити.

Причина: серед його tools є деструктивні. Повний перелік з `ArduinoBundle.properties`:

`build` · `flash` · `monitor` · `set_target` · `clean_build` · **`erase_flash`** · `get_size_info` · `decode_backtrace` · `get_partition_table` · `list_boards`

Це **не** стандартний MCP-транспорт (не JSON-RPC/SSE), а простий HTTP API — для під'єднання до MCP-клієнта потрібен був би шим.

---

## 4. Ліцензія

`plugin.xml` містить `<product-descriptor code="PSCIPIOAR" release-date="20260310" release-version="20261" optional="true" />` — платний плагін з тріалом.

Критичний рядок з `ArduinoBundle.properties`:

```
notification.license.expired.title=Flexible Arduino License Expired
notification.license.expired.message=Your license is either expired or unregistered
  and the plugin has been disabled. ...
```

Тобто після тріалу вимикається **весь плагін**, а не лише Pro-функції. Перевіряти статус: Settings → Plugins → Flexible For Arduino.

**Pro-функції** (потребують ліцензії): JTAG/GDB-дебаг через OpenOCD, OTA-аплоад, Memory Analyzer, Device Profiler. Поле `openOcdPath` — для JTAG; поки не заповнене.

---

## 5. Мовна підтримка: що плагін дає і чого не дає

### Дає (для `.ino`, власна мова `FlexibleArduino`)

Підсвітка синтаксису з окремою колірною схемою · автодоповнення (Arduino builtins, dot-completion для `Serial.` / `Wire.` / `SPI.`, локальні змінні та функції в області видимості) · hover-документація з Arduino language reference · structure view · folding (функції, класи, коментарі, `#if`/`#ifdef`) · форматер, brace matcher, quote handler, commenter · семантичні аннотатори.

### Не дає — ctrl+click і Find Usages

У `META-INF/plugin.xml` **нуль** входжень `psi.referenceContributor`, `gotoDeclarationHandler`, `referencesSearch`. Тобто **Go to Definition, Find Usages і навігація по посиланнях у `.ino` — відсутні як функціональність**, це не питання налаштування.

### `.cpp` / `.h` плагін не обробляє взагалі

Джерела цього проєкту — `src/*.cpp`, `include/*.h`, `.ino` немає. Для них плагін — лише build-оркестратор. Навігація, автодоповнення по бібліотеках (TFT_eSPI, LVGL, arduino-esp32) і ctrl+click мали б іти через `com.intellij.clion` + `compile_commands.json`.

Що цьому зараз перешкоджає:

- `compile_commands.json` **застарілий**: 2 серпня, 427 записів, і лише для env `esp32-st7789`
- модуль описаний як `JAVA_MODULE` зі «SDK» типу `ESP-IDF` (`.idea/esp32.iml`: `jdkName="ESP-IDF v6.0.2" jdkType="ESP-IDF"`, те саме в `misc.xml`) — жодної C/C++ конфігурації в проєкті немає
- **не перевірено**, чи IntelliJ IDEA (не CLion) з плагіном `com.intellij.clion` реально підхоплює `compile_commands.json` як Compilation Database. У CLion це підтримується; для IDEA потрібна перевірка. Якщо ні — навігацію робити в CLion, а Flexible лишити як build-оркестратор в IDEA.

### Конфлікт за розширення `.ino`

`plugin.xml` реєструє `<fileType name="Arduino" extensions="ino" language="FlexibleArduino">`. Відповідно в `~/.config/JetBrains/IntelliJIdea2026.2/options/filetypes.xml` з'явилось:

```xml
<removed_mapping ext="ino" approved="true" type="C/C++ Header" />
```

`.ino` віддано плагіну, C/C++ від нього відрізано. **Два механізми взаємовиключні:** щоб отримати ctrl+click у `.ino`, треба в Settings → Editor → File Types додати `*.ino` до `C++` — і тим вимкнути Arduino-специфічні функції плагіна для цих файлів. Плюс `.ino` не є валідним C++ сам по собі (немає `#include <Arduino.h>`, функції неоголошені), тому без записів у compilation database буде багато хибних помилок.

---

## 6. Як плагін збирає (декомпіляція `PlatformIoBackend`)

- `buildCommandLine(Project, String)` → запускає `pio` у корені проєкту (`effectiveWorkDir`), **без `-e`**
- `flashCommandLine` → додає `--target upload --upload-port <нормалізований порт з comPort>`
- `monitorCommandLine` → `device monitor --port <...> --baud <serialBaudRate>`
- отже **режим `BUILD` порт не чіпає взагалі** — це підтверджено на рівні байткоду

**Наслідок для збірки всіх env:** `[platformio]` у `platformio.ini` не має `default_envs`, тому `pio run` без `-e` пройде по всіх семи:

`esp32-4848s040` · `esp32-s3-lcd147` · `esp8266` · `esp32-st7789` · `ttgo-t1` · `esp32-c6` · `esp32-c6-lcd096`

**Обмеження:** у run config немає вибору PIO env. Поле `buildTarget` — це board target плагіна (`esp32dev`, `esp32-s3-devkitc-1`, `megaatmega2560`, `nanoatmega328`, `d1_mini`), а **не** PIO environment. Для збірки одного env — термінал: `pio run -e esp32-s3-lcd147`.

**Ризик:** env `esp8266` вимагає платформи `espressif8266`. Якщо вона не встановлена, «збірка всіх env» упаде саме на ньому.

---

## 7. Сторінка налаштувань

`Settings → Languages & Frameworks → Arduino/ESP-IDF` (`projectConfigurable`, `parentId="language"`).

Секції та поля:

| Секція | Поля |
| :--- | :--- |
| Build Backend | `Build backend:` — `auto (detect from project)` / `esp-idf (use idf.py)` / `arduino-cli (use arduino-cli)` / `platformio (use pio)` / `zephyr (use west)` |
| SDK Configuration | `ESP-IDF path:`, `Python path:`, `Arduino CLI path:`, `PlatformIO (pio) path:`, `West path:`, `Zephyr Base (ZEPHYR_BASE):`, кнопка `Manage SDKs in Project Structure` |
| Device Settings | `COM Port:`, `Baud Rate:`, `Board Target:`, `FQBN (Arduino CLI board ID):`, `Zephyr Board:`, `Custom board targets:`, `Board Manager URLs:` |
| Flash Settings | `Flash Mode:`, `Flash Size:`, `Flash baud rate:` (`115200`/`230400`/`460800`/`921600`), `Upload method:` (`UART`/`JTAG`/`DFU`), `Partition table:` (`default`/`min`/`min_spiffs`/`huge_app`/`no_ota`) |
| Serial Monitor | `COM Port:`, `Baud Rate:`, `Timestamp format:`, `Detect boot loops and show diagnostics` |
| MCP Server | `Enable MCP Server`, `MCP Port:` |
| Libraries | Library Manager (пошук/встановлення) |
| OTA Manager | `Hostname / IP:`, `OTA Port:`, `Password:`, `Firmware (.bin):`, `Scan Network`, `Upload History` |
| General | `Sketch path:`, `Build output directory:`, `Save all files before build` |

Інші точки входу: `Tools → Arduino` (усі дії, включно з `Welcome & Guided Tour`, `Browse Examples...`, `Initialize Arduino Project...`, `Import Arduino Project...`), tool window **Arduino** внизу (вкладки Serial Monitor, Pin Viewer, Device Profiler), tool window **sdkconfig** справа.

---

## 8. Хоткеї

| Комбінація | Дія | Безпека, поки плата в роботі |
| :--- | :--- | :--- |
| `Ctrl+Alt+R` | Build Project | безпечно |
| `Ctrl+Alt+U` | Build and Flash | **небезпечно** |
| `Ctrl+Alt+M` | Build, Flash and Monitor | **небезпечно** |

Так само небезпечні пункти `Tools → Arduino`: `Flash to Device`, `Build and Flash`, `Build, Flash and Monitor`, `Upload Only (skip build)`, `OTA Upload`, `Open Serial Monitor`.

---

## 9. Безпечні команди

Перегенерувати `compile_commands.json` під конкретний env (плату не чіпає):

```sh
cd ~/Work/ESP32/esp32
~/.platformio/penv/bin/pio run -t compiledb -e esp32-s3-lcd147
```

`compiledb` не показується у `pio run --list-targets`, але є вбудованим таргетом PlatformIO 6. Фолбек: `pio run -t idedata -e <env>`.

Compilation database для скетча без компіляції та заливки (флаг наявний в `arduino-cli` 1.5.1):

```sh
arduino-cli compile --only-compilation-database \
  --fqbn esp32:esp32:esp32c6 --build-path /tmp/ino-cdb <шлях-до-скетча>
```

Визначення плати без відкриття порту — читає лише USB VID/PID, ресета не викликає:

```sh
arduino-cli board list      # ESP32-S3 DevKitC визначається як VID:PID 303a:1001
arduino-cli board listall | grep -i esp32s3    # підбір FQBN
```

Перевірка, що IDE не тримає порт відкритим:

```sh
lsof /dev/ttyACM0     # має бути порожньо, окрім вашого робочого процесу
```

---

## 10. Чекліст на момент повернення

1. Відкрити проєкт саме в **IDEA 2026.2** (у 2026.1 плагіна немає).
2. Перевірити **ліцензію/тріал**: Settings → Plugins → Flexible For Arduino. Якщо тріал вичерпаний — плагін вимкнений повністю, решта кроків марна.
3. Виправити run config: `mode="BUILD_FLASH_MONITOR"` → **`Build`**.
4. Вимкнути в Serial Monitor: `Reset device on connect`, `Assert DTR on connect`, `Auto-reconnect`, за потреби `Detect boot loops`. Переконатись, що вони **явно** з'явились у `arduino-settings.xml` як `false`.
5. Порт лишити **порожнім**, поки п.4 не зроблений.
6. `Enable MCP Server` не вмикати (містить `flash`, `erase_flash`).
7. Перевірити збірку: `Ctrl+Alt+R`. Врахувати, що піде по всіх 7 env; можливий фейл на `esp8266` через відсутню платформу.
8. Для навігації перегенерувати `compile_commands.json` під активний env і перевірити, чи IDEA підхоплює compilation database (п.5 — відкрите питання).
9. Тільки після цього — вмикати flash / monitor, і лише коли плата вільна.
10. JTAG/OpenOCD + Memory Analyzer — окремим етапом (Pro).
