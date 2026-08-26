# Плагін «Flexible For Arduino» для JetBrains IDE — аудит і налаштування

Цей документ фіксує аудит плагіна `Flexible For Arduino` в IntelliJ IDEA та стан його конфігурації в цьому проєкті: версії, шляхи, декомпільовані дефолти, знайдені небезпечні місця та чекліст дій.

**Стан на 25 серпня 2026: налаштування завершене.** Ліцензія — `purchased`. Небезпечні дефолти вимкнені, run configs розділені на безпечний і заливальний, збірка / flash / serial monitor перевірені на живій платі ESP32-C6FH8. Журнал — розділ 12. Лишилась одна дія в UI: зв'язати проєкт з compilation database (розділ 13).

Дані отримані розбором `flexible-arduino-2026.1.18.jar` (`META-INF/plugin.xml`, `messages/ArduinoBundle.properties`, байткод `ArduinoSettingsState` і `PlatformIoBackend`) та аудитом `.idea/` цього проєкту.

---

## 1. Статус

Плагін встановлений (19.08.2026) і налаштований для цього проєкту: `activeBackend=platformio`, основний env — `esp32-c6`.

Налаштування 25.08.2026 йшло у два заходи:

1. **без плати** — плата була фізично відключена (жодного `/dev/ttyACM*`), тому конфігурація робилась із нульовим ризиком випадкового flash чи reset: небезпечні дефолти, run config, `default_envs`, compilation database;
2. **з платою на `/dev/ttyACM0`** — після того, як плату під'єднали і ліцензія підтвердилась як `purchased`: перевірені flash, serial monitor, і з'ясована апаратна межа захисту від ресету (розділ 3.5).

Плата: **ESP32-C6FH8 (QFN32) rev v0.2**, 8 MB embedded flash, USB-Serial/JTAG, MAC `ac:eb:e6:2b:66:a4` — збігається з основним env `esp32-c6` (`board_upload.flash_size = 8MB`).

`.ino`-сценарій (скетчі через `arduino-cli`) виведений зі скоупу — у роботі не задіяний.

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

Поточний вміст `.idea/arduino-settings.xml` цього проєкту (після налаштування 25.08.2026):

```xml
<component name="ArduinoSettingsState">
  <option name="espIdfPath" value="$USER_HOME$/.espressif/v6.0.2/esp-idf" />
  <option name="pythonPath" value="$USER_HOME$/.platformio/penv/bin/python" />
  <option name="arduinoCliPath" value="$USER_HOME$/.local/bin/arduino-cli" />
  <option name="platformioPath" value="$USER_HOME$/.platformio/penv/bin/pio" />
  <option name="activeBackend" value="platformio" />
  <option name="comPort" value="" />
  <option name="serialPort" value="" />
  <option name="monitorResetOnConnect" value="false" />
  <option name="monitorDtrOnConnect" value="false" />
  <option name="monitorRtsOnConnect" value="false" />
  <option name="monitorAutoReconnect" value="false" />
  <option name="usbAutoDetect" value="false" />
  <option name="monitorBootLoopDetection" value="false" />
  <option name="mcpEnabled" value="false" />
</component>
```

Порт лишений **порожнім** свідомо: разом із `usbAutoDetect=false` це означає, що плагін не візьме порт сам, навіть коли плату під'єднають. Порт задається вручну перед flash.

`monitorRtsOnConnect` і `mcpEnabled` були прописані явно, хоча їхній дефолт і так `false`.

**Прогноз із 3.3 підтвердився.** Після того, як користувач відкрив проєкт в IDE і вписав порт, IDE перезаписала файл:

- усі **5 опцій із дефолтом `true`** (`monitorResetOnConnect`, `monitorDtrOnConnect`, `monitorAutoReconnect`, `usbAutoDetect`, `monitorBootLoopDetection`) залишились у файлі як `false` — захист вижив;
- **2 опції з дефолтом `false`** (`monitorRtsOnConnect`, `mcpEnabled`) IDE прибрала як «рівні дефолту» — поведінка не змінилась;
- `comPort` / `serialPort` стали `ttyACM0`;
- `mode="BUILD"` і `buildTarget="esp32c6"` у `workspace.xml` збереглись.

Тобто серіалізатор працює саме так, як описано в 3.3, і важливі опції він **не** втрачає. Фактичний вміст файлу зараз — той самий список мінус два прибраних рядки.

Також є `.idea/arduino-subprojects.xml` з `activeSubprojectPath = "."`.

---

## 3. Небезпечні місця

### 3.1. Run configuration названий «Build», але заливав прошивку — ВИПРАВЛЕНО

Було в `.idea/workspace.xml`: назва «Arduino Build», а режим — `BUILD_FLASH_MONITOR`, тобто збірка **плюс заливка плюс відкриття монітора**.

Enum `ArduinoRunConfiguration$RunMode`:

`BUILD` · `FLASH` · `UPLOAD_ONLY` · `MONITOR` · `BUILD_AND_FLASH` · `BUILD_FLASH_MONITOR` · `DEBUG`

Зараз:

```xml
<configuration name="Arduino Build" type="ARDUINO_RUN" factoryName="ARDUINO_RUN_FACTORY">
  <options mode="BUILD" buildTarget="esp32c6" additionalArgs=""
           cleanFirst="false" port="" baudRate="921600"
           subprojectPath="" sketchFolder="" programmer="" />
</configuration>
```

Плюс додані дві окремі конфігурації, щоб заливка була свідомим вибором, а не побічним ефектом `Ctrl+Alt+R`:

| Конфігурація | Mode | Baud | Що робить |
| :--- | :--- | :--- | :--- |
| `Arduino Build` | `BUILD` | 921600 | лише збірка, порт не чіпає — на `Ctrl+Alt+R` |
| `Arduino Flash` | `BUILD_AND_FLASH` | 921600 | збірка + заливка, **без** монітора |
| `Arduino Monitor` | `MONITOR` | 115200 | лише монітор (ресетне плату, розділ 3.5) |

Свідомо **не** заведено конфігурації в режимі `BUILD_FLASH_MONITOR` — саме вона й була початковою пасткою.

Правка зроблена напряму у файлі — це безпечно **лише** коли проєкт не відкритий у запущеній IDE (інакше IDE перезапише `workspace.xml` при виході). Перевірка перед правкою:

```sh
readlink /proc/$(pgrep -f 'bin/idea$' | head -1)/fd/* 2>/dev/null | grep -c 'Work/ESP32/esp32'
```

`0` — проєкт не завантажений, файл правити можна. Інакше йти через Run → Edit Configurations → `Arduino Build` → Mode = `Build`.

### 3.2. Дефолти монітора ресетять ESP32 — ПЕРЕВИЗНАЧЕНО

З декомпіляції конструктора `ArduinoSettingsState`. Колонка «Зараз» — фактичне значення після налаштування 25.08.2026:

| Поле | Дефолт | Наслідок | Зараз |
| :--- | :--- | :--- | :--- |
| `monitorResetOnConnect` | `true` | **ресет пристрою при підключенні монітора** | `false` — але на C6 не допомагає, див. 3.5 |
| `monitorDtrOnConnect` | `true` | DTR на ESP32 = ресет | `false` — те саме, 3.5 |
| `monitorRtsOnConnect` | `false` | — | `false` |
| `monitorAutoReconnect` | `true` | **сам відкриває порт** | `false` |
| `usbAutoDetect` | `true` | автопідхват порту | `false` |
| `monitorBootLoopDetection` | `true` | читає порт | `false` |
| `mcpEnabled` | `false` | — | `false` |
| `mcpPort` | `3725` | — | дефолт |
| `saveBeforeBuild` | `true` | безпечно | дефолт |
| `flashBaudRate` | `460800` | — | дефолт |
| `baudRate` / `serialBaudRate` | `115200` | — | дефолт |
| `boardTarget` | `esp32` | лише Pin Viewer | дефолт |
| `flashMode` | `dio` | — | дефолт |
| `flashSize` | `4MB` | — | дефолт |
| `uploadMethod` | `UART` | варіанти: `UART` / `JTAG` / `DFU` | дефолт |
| `lineEnding` | `CRLF` | — | дефолт |
| `monitorEncoding` | `UTF-8` | — | дефолт |
| `monitorTimestampFormat` | `HH:mm:ss.SSS` | — | дефолт |
| `activeBackend` | `auto` | — | `platformio` |
| `otaPartitionScheme` | `default` | — | дефолт |

### 3.3. Пастка «порожнього XML»

IntelliJ `XmlSerializerUtil` серіалізує **лише значення, відмінні від дефолтних**. Тому:

> **Відсутність поля в `arduino-settings.xml` означає «увімкнено за дефолтом», а не «вимкнено».**

Саме через це до 25.08.2026 `monitorResetOnConnect`, `monitorDtrOnConnect`, `monitorAutoReconnect` і `usbAutoDetect` були **активні**, хоча в XML їх не було. Тепер вони прописані явно як `value="false"` (розділ 2).

Обернений бік того ж правила: явно виписані `false` там, де дефолт і так `false` (`monitorRtsOnConnect`, `mcpEnabled`), IntelliJ при наступному збереженні з UI приберe — вони «рівні дефолту». Це не регресія. А от `false` у полях з дефолтом `true` серіалізатор збереже завжди — тобто саме важливі чотири опції залишаться у файлі.

### 3.4. Чим захищено зараз

Раніше єдиним стоп-краном був неіснуючий порт: у конфігу стояв `ttyACM1`, а плата була на `ttyACM0`, тому плагін нічого не відкривав. Захист випадковий — досить було виправити порт «щоб працювало», і монітор одразу ресетнув би плату.

Зараз захист явний і не залежить від номера порту:

1. `usbAutoDetect=false` — плагін не підбирає порт сам, навіть коли плату під'єднають;
2. `monitorAutoReconnect=false` — монітор не піднімається самостійно;
3. run config `Arduino Build` у режимі `BUILD` — порт не чіпається взагалі (підтверджено байткодом, розділ 6);
4. `monitorResetOnConnect=false` + `monitorDtrOnConnect=false` — **не є захистом на C6**, див. 3.5 (плагін виставляє лінії явно, а саме це й ресетить чип).

Порт (`comPort` / `serialPort` = `ttyACM0`) вписаний вручну 25.08.2026 після під'єднання плати. Це свідома дія, а не автопідхват: із `usbAutoDetect=false` плагін сам би його не взяв.

**Що з цього реально тримає ресет.** Оскільки пункт 4 на цьому чипі не працює, єдиний робочий бар'єр — пункти 1–3, тобто «порт ніхто не відкриває сам». Стоп-кран перемістився з «неправильний порт» на «жодного автоматичного відкриття порту».

### 3.5. На ESP32-C6 ресетить не відкриття порту, а зміна DTR/RTS

> **Уточнено 26.08.2026. Попередній висновок цього розділу був хибним** — нижче
> виправлена версія, стара залишена в кінці для історії.

Ресет спричиняє **дотик до ліній DTR/RTS**, а не сам факт відкриття CDC-порту. Якщо
відкрити порт і не торкатись їх узагалі — плата продовжує працювати, стан у RAM цілий.

Перевірено 26.08.2026: три відкриття порту поспіль, лічильник повідомлень наскрізь
зростає (4371 → 4412 → 4453), ROM boot у виводі відсутній. Пізніший прогін через `./esp`:
5282 → 5499 → 5506.

**Парадокс, на якому легко обпектися:** `dtr=False` / `rts=False` — це **сама по собі
зміна стану ліній**. Тому вони ресет не прибирають, а викликають. Саме через це раніше
здавалося, що «ресет апаратний і його нічим не вимкнути»: усі спроби «захиститись»
виставленням нулів і були причиною. Так само поводиться `pio device monitor` — він явно
друкує `--- forcing DTR inactive` / `--- forcing RTS inactive` і закономірно отримує
`rst:0x15 (USB_UART_HPSYS)`.

Наслідки:

- **говорити з платою без втрати стану можна.** `serial.Serial(port, baud, timeout=…)` і
  жодного `dtr`/`rts`/`stty`. Готовий інструмент — `./esp <команда>` у корені репозиторію
  (у шапці скрипта це зафіксовано, щоб ніхто не «полагодив» його, додавши DTR/RTS);
- `monitorResetOnConnect=false` / `monitorDtrOnConnect=false` — усе одно **не бар'єр**:
  плагін виставляє лінії явно, тобто робить те, що й спричиняє ресет;
- DTR/RTS лишається робочим способом **навмисно** ресетнути плату (наприклад, щоб
  зловити лог із моменту старту або вивести з boot-loop);
- захист із 3.4 («жодного автоматичного відкриття порту») лишається доречним для роботи
  через IDE, але тепер це питання зручності, а не єдиний спосіб зберегти стан.

<details>
<summary>Стара (хибна) версія розділу — для історії</summary>

Перевірено емпірично 25.08.2026 на живій платі. Порт відкривався з `dtr=False`,
`rts=False`, виставленими **до** `open()`:

| Сесія | ROM boot у виводі | Макс. uptime у логах |
| :--- | :--- | :--- |
| 1 (14 с) | так | 9012 мс |
| 2 (через 6 с паузи, 4 с) | так | 3601 мс |

Тодішній висновок: «чип ресетнувся від самого факту відкриття CDC-порту», а «єдиний
спосіб не ресетнути плату — не відкривати порт». Помилка була в тому, що обидві сесії
виставляли DTR/RTS — тобто вимірювався не «факт відкриття», а «відкриття зі зміною ліній».

</details>

Автор проєкту вже наступав на суміжні граблі: у `[env:esp32-c6]` стоїть `monitor_rts = 0` / `monitor_dtr = 0` з коментарем про зависання монітора на C6 під Linux (при тому, що в `[env]` обидва `= 1`). Налаштування плагіна тепер узгоджені з цим.

### 3.6. MCP-сервер плагіна

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

Тобто після тріалу вимикається **весь плагін**, а не лише Pro-функції.

**Статус на 25.08.2026: `purchased`** (перевірено в Settings → Plugins → Flexible For Arduino). Тріал не є обмеженням, Pro-функції доступні. У файлах на диску ліцензія не відображається — тільки в UI.

**Pro-функції** (потребують ліцензії): JTAG/GDB-дебаг через OpenOCD, OTA-аплоад, Memory Analyzer, Device Profiler. При статусі `purchased` вони відкриті, але ще не налаштовані — `openOcdPath` порожній.

Для ESP32-C6 JTAG окремий адаптер не потрібен: чип має вбудований USB-Serial/JTAG (підтверджено esptool: `USB mode: USB-Serial/JTAG`), тобто дебаг можливий по тому самому кабелю.

---

## 5. Мовна підтримка: що плагін дає і чого не дає

### Дає (для `.ino`, власна мова `FlexibleArduino`)

Підсвітка синтаксису з окремою колірною схемою · автодоповнення (Arduino builtins, dot-completion для `Serial.` / `Wire.` / `SPI.`, локальні змінні та функції в області видимості) · hover-документація з Arduino language reference · structure view · folding (функції, класи, коментарі, `#if`/`#ifdef`) · форматер, brace matcher, quote handler, commenter · семантичні аннотатори.

### Не дає — ctrl+click і Find Usages

У `META-INF/plugin.xml` **нуль** входжень `psi.referenceContributor`, `gotoDeclarationHandler`, `referencesSearch`. Тобто **Go to Definition, Find Usages і навігація по посиланнях у `.ino` — відсутні як функціональність**, це не питання налаштування.

### `.cpp` / `.h` плагін не обробляє взагалі

Джерела цього проєкту — `src/*.cpp`, `include/*.h`, `.ino` немає. Для них плагін — лише build-оркестратор. Навігація, автодоповнення по бібліотеках (TFT_eSPI, LVGL, arduino-esp32) і ctrl+click мали б іти через `com.intellij.clion` + `compile_commands.json`.

Стан і те, що ще перешкоджає:

- `compile_commands.json` **перегенерований 25.08.2026** під env `esp32-c6`: 589 записів, 25 МБ (`pio run -t compiledb -e esp32-c6`, 6.7 с). Було: 2 серпня, 427 записів, env `esp32-st7789`.
- ~~модуль описаний як `JAVA_MODULE` зі «SDK» типу `ESP-IDF`~~ — **вже не так.** Після зв'язування з CDB імпортер прибрав `.idea/modules.xml` і `.idea/esp32.iml` і замінив модель проєкту на свою (`CompDBSettings` + `CompDBWorkspace` у `misc.xml`). Залишком старої моделі лишився безхозний рядок у `misc.xml`:

  ```xml
  <component name="ProjectRootManager" version="2" languageLevel="JDK_1_6"
             default="true" project-jdk-name="ESP-IDF v6.0.2" project-jdk-type="ESP-IDF">
  ```

  C/C++ резолв він не блокує; його тримає плагін Flexible під свій тип SDK.
- **бракує плагіна.** Відкрите питання розв'язане: сам `com.intellij.clion` compilation database **не читає**. Його опис у `plugin.xml` прямо каже, що це лише інфраструктура без UI:

  > *«Provides the shared infrastructure for C/C++ development… It exposes toolchain, debugger, build-system, run/debug, and project-model APIs used by the CLion C and C++, CMake, Meson, **Compilation Database**, vcpkg, and test-framework plugins. No standalone UI or features.»*

  Тобто `Compilation Database` — **окремий плагін**:

  | | |
  | :--- | :--- |
  | Плагін | `Compilation Database` |
  | xmlId | `com.intellij.clion-compdb` |
  | Marketplace | `https://plugins.jetbrains.com/plugin/28800` (посилання з опису: `https://jb.gg/compdbplugin`) |
  | Ціна | безкоштовний (`purchaseInfo: null`) |
  | **Встановлений** | **так, 25.08.2026, версія `262.8665.176`** |

  Тепер у проєкті стоять усі три потрібні плагіни:

  | Плагін | id | Версія |
  | :--- | :--- | :--- |
  | Native Build Tools (інфраструктура, без UI) | `com.intellij.clion` | 262.9437.185 |
  | CLion C and C++ (бекенд Radler) | `org.jetbrains.plugins.clion.radler` | 262.9437.185 |
  | Compilation Database | `com.intellij.clion-compdb` | 262.8665.176 |

### Лишилось зв'язати проєкт з compilation database

Встановленого плагіна **недостатньо** — проєкт треба явно залінкувати з `compile_commands.json`. Станом на кінець сесії це **не зроблено**: у `.idea/` немає жодної згадки `compdb`.

`compile_commands.json` лежить у корені проєкту, згенерований під `esp32-c6` — усе готове до зв'язування.

**Дія (тільки в UI):** правий клік на `compile_commands.json` у Project view → **`Load Compilation Database Project`**. Альтернатива — Settings → Build, Execution, Deployment → Compilation Database → **`Select compile_commands.json`**.

Точні тексти дій узяті з `messages/CompDBBundle.properties` плагіна:

| Ключ | Текст |
| :--- | :--- |
| `action.CompDB.LoadMakefileProject.text` | `Load Compilation Database Project` |
| `project.status.action.select` | `Select compile_commands.json` |
| `refresh.project.action.name` | `Reload Compilation Database Project` |
| `configurable.empty.text` | `No Compilation Database project detected` |

Останній рядок — те, що зараз показує сторінка налаштувань, поки проєкт не залінкований. Це і є індикатор.

**Перевірка з CLI.** `CompDBLocalSettings` — це `@State(name="CompDBLocalSettings", storages=@Storage("$WORKSPACE_FILE$"))`, тобто після зв'язування компонент з'явиться у `.idea/workspace.xml`:

```sh
grep -c CompDBLocalSettings .idea/workspace.xml   # 0 = не залінковано
```

**Після кожної регенерації `compile_commands.json`** (зміна env, нові файли, нові `lib_deps`) треба виконати `Reload Compilation Database Project` — інакше індекс залишиться від старої збірки.

**Чого очікувати.** Саме це має дати ctrl+click і Find Usages по `.cpp`/`.h` та бібліотеках (TFT_eSPI, LVGL, arduino-esp32) — того, чого Flexible не вміє в принципі.

### Пастка: PlatformIO пише відносні шляхи, і плагін їх не мапить

**Симптом.** У `platformio.ini` для env стоїть, наприклад, `-D DISPLAY_SPLIT_COUNT=4`, а в редакторі `#if defined(DISPLAY_SPLIT_COUNT) && DISPLAY_SPLIT_COUNT` підсвічується як **невиконана умова** — код під ним сірий, символи з нього не резолвяться.

**Причина.** `pio run -t compiledb` генерує CDB, у якому шляхи всередині проєкту — **відносні**, а зовнішні — абсолютні. У нашому випадку: 487 записів із 589 мали відносний `file`, усі 589 — відносний `output`, і 924 токени `-I`/`-isystem` теж були відносними:

```json
{
  "file": "src/Display.cpp",
  "output": ".pio/build/esp32-c6/src/Display.cpp.o",
  "command": "… \"-I.pio/libdeps/esp32-c6/GFX Library for Arduino/src\" …",
  "directory": "/home/nick/Work/ESP32/esp32"
}
```

Формально це валідний JSON Compilation Database — відносні шляхи резолвяться від `directory`. Але плагін не мапить такі записи на файли в редакторі: файл вважається таким, що **не належить до проєкту**, IDE показує банер і пропонує `Highlight anyway`.

**Чому саме це ламає макроси.** У режимі форсованої підсвітки C/C++ парситься **без флагів компіляції** — тобто без жодного `-D`. Тому будь-який `#if defined(МАКРОС)` резолвиться як false, хоч макрос і заданий у `build_flags`. Слід залишається в `.idea/workspace.xml`:

```xml
<component name="HighlightingSettingsPerFile">
  <setting file="file://$PROJECT_DIR$/src/Display.cpp" root0="FORCE_HIGHLIGHTING" />
  <setting file="file://$PROJECT_DIR$/src/main.cpp" root0="FORCE_HIGHLIGHTING" />
</component>
```

**Наявність тут `.cpp`/`.h` файлів — це діагноз, а не налаштування.** Вона означає, що резолв іде без макросів.

**Фікс.** Зробити всі шляхи в CDB абсолютними — скриптом `tools/normalize_compdb.py` (див. 9). Він нормалізує `file`, `output` і `-I`/`-isystem`, коректно обробляючи шляхи з пробілами (`GFX Library for Arduino`). Порядок:

```sh
~/.platformio/penv/bin/pio run -t compiledb -e esp32-c6
python3 tools/normalize_compdb.py
```

Далі в IDE — `Reload Compilation Database Project`. І прибрати з `workspace.xml` записи `FORCE_HIGHLIGHTING` для `.cpp`/`.h` (при закритій IDE), інакше файли залишаться в режимі підсвітки без флагів.

**Перевірка, що CDB справді робочий** — прогнати його команду через компілятор:

```sh
python3 - <<'PY'
import json, shlex, subprocess
d = json.load(open('compile_commands.json'))
c = [x for x in d if x['file'].endswith('/src/Display.cpp')][0]
t = [x for x in shlex.split(c['command']) if x not in ('-c',)]
i = t.index('-o'); del t[i:i+2]
t.insert(1, '-fsyntax-only')
r = subprocess.run(t, cwd=c['directory'], capture_output=True, text=True)
print('exit', r.returncode, r.stderr[:400])
PY
```

`exit 0` без stderr означає, що проблема не в CDB, а в тому, як його читає IDE. Перевірити сам макрос:

```sh
… -dM -E - < /dev/null | grep DISPLAY_SPLIT_COUNT   # -> #define DISPLAY_SPLIT_COUNT 4
```

**Хедерів у CDB немає взагалі** (0 із 589) — це нормально для compilation database: записи є лише для translation units, тобто `.cpp`/`.c`. Хедер (`src/Display.h`) отримує флаги від TU, який його включає. Якщо в самому хедері `#if` резолвиться неправильно, а в `.cpp` — правильно, то річ у виборі контексту хедера, а не в макросах: у CLion-бекенді для цього є перемикач контексту над редактором.

### Структура `platformio.ini` — потрібен плагін `Ini`

У IntelliJ IDEA **немає вбудованої підтримки `.ini`**: у `lib/*.jar` платформи немає ні INI-мови, ні file type (перевірено пошуком по `IniFileType`/`lang/ini`). У `filetypes.xml` для `.ini` теж немає жодного маппінгу — тобто `platformio.ini` відкривається як звичайний текст, без structure view.

Flexible тут не допомагає: він реєструє `lang.psiStructureViewFactory` **тільки** для мови `FlexibleArduino` (`.ino`) і має окремий file type для `sdkconfig`/`sdkconfig.defaults` — але для `platformio.ini` нічого.

Закривається офіційним плагіном JetBrains:

| | |
| :--- | :--- |
| Плагін | `Ini` |
| xmlId | `com.jetbrains.plugins.ini4idea` |
| Marketplace | `https://plugins.jetbrains.com/plugin/6981` |
| Версія | `262.8665.176`, `since 262.8665`, `until 262.*` — сумісна з IU-262.9437.185 |
| Ціна | безкоштовний |
| Розмір | 62 КБ |

Що дає (з `META-INF/plugin.xml`):

| Extension point | Наслідок |
| :--- | :--- |
| `lang.psiStructureViewFactory` | **Structure view** (`Alt+7`, popup `Ctrl+F12`) — три рівні: файл → секція → ключ |
| `lang.foldingBuilder` | folding секцій — можна скласти всі сім `[env:*]` |
| `lang.parserDefinition` + `lang.syntaxHighlighterFactory` | справжня підсвітка замість plain text |
| `lang.formatter`, `lang.commenter` | форматування і `Ctrl+/` |
| `localInspection DuplicateSectionInFile` | дубльовані секції |
| `localInspection DuplicateKeyInSection` | **дубльовані ключі в секції** — на 1130 рядків і 7 env корисно |
| `spellchecker.support` | перевірка орфографії в коментарях |

**Багаторядкові значення підтримуються** — у лексері є токен `MULTILINE_VALUE_PART`, тобто `build_flags` і `lib_deps` з відступом не підсвічуються як помилки.

**А inline-коментарі — ні.** Наявність токена `ONE_LINE_COMMENT` спершу здалась достатньою, але вона стосується лише коментаря, що **починає** рядок (з відступом теж). Коментар після значення:

```ini
-D FLIP_BUTTON_PIN=9      ; BOOT button (docs.waveshare.com/ESP32-C6-Touch-LCD-1.47)
```

залишається частиною значення й не сіріє. Це **не налаштовується**:

- у `_IniLexer` (JFlex) `;` і `#` мають **однаковий клас символів** (обидва — клас 3 у `ZZ_CMAP`), тобто заміна `;` на `#` нічого не змінить;
- плагін не реєструє жодного `Configurable` — лише `colorSettingsPage` з ключами `Property key`, `Property value`, `Section`, `Comment`, `Equal`. Тобто перекрасити те, що плагін **вже** вважає коментарем, можна; змінити правила лексера — ні, вони в DFA.

**Практичний обхід** — виносити коментар окремим рядком:

```ini
build_flags =
    ; BOOT button (docs.waveshare.com/ESP32-C6-Touch-LCD-1.47)
    -D FLIP_BUTTON_PIN=9
```

Так він підсвічується, і PlatformIO це переживає: у `platformio.ini` вже є закомментовані рядки з відступом усередині `build_flags` (напр. 991–992, 553), і в CDB не протік жоден токен, що починається з `;` або `#`.

**На збірку inline-коментарі не впливають** — PIO їх ріже коректно. Перевірено: рядок 995 `-D FLIP_BUTTON_PIN=9      ; BOOT button (...)` доїжджає до компілятора як чистий `-DFLIP_BUTTON_PIN=9`. Тобто це рівно косметика.

Альтернатива, якщо inline-коментарі важливіші за структуру: завести **User-defined file type** (Settings → Editor → File Types) для `platformio.ini` із `Line comment = ;`. У custom file types коментар підсвічується будь-де в рядку. Ціна — файл перестає бути мовою `Ini`, тобто зникають structure view, folding і інспекції. Вибір взаємовиключний.

Плагін забирає розширення `cfg;ini`. У проєкті це `platformio.ini` і `secrets.ini`, `.cfg`-файлів немає — конфліктів не буде.

### Чому не `PlatformIO for CLion`

Є другий офіційний плагін — `PlatformIO for CLion` (`intellij.clion.embedded.platformio`, ID 13922, безкоштовний, теж сумісний). Він **залежить** від `ini4idea` і сам додає лише окремий file type для `platformio.ini` на тій самій мові `Ini`, плюс свої run configurations і before-run tasks (build / upload / clean).

Ставити його **зараз не варто**: у нього власні `projectOpenProcessor`, `workspaceProvider` і `projectSettings` — тобто він створює свою модель проєкту, яка конкурувала б із щойно налаштованою Compilation Database, і дублював би run-конфігурації Flexible. Для структури `.ini` достатньо самого `Ini`.

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

**Наслідок для збірки всіх env — знято через `default_envs`.** Раніше `[platformio]` не мав `default_envs`, тому `pio run` без `-e` йшов по всіх семи:

`esp32-4848s040` · `esp32-s3-lcd147` · `esp8266` · `esp32-st7789` · `ttgo-t1` · `esp32-c6` · `esp32-c6-lcd096`

25.08.2026 у `[platformio]` додано:

```ini
[platformio]
default_envs = esp32-c6
```

Тепер і `Ctrl+Alt+R`, і `pio run` у терміналі збирають лише `esp32-c6`. Перевірено: **SUCCESS за 34.5 с**. Явний `-e` як завжди перекриває default.

**Обмеження:** у run config немає вибору PIO env — env керується тільки через `default_envs` або `-e` у терміналі.

**Що таке `buildTarget` у run config.** Це **не** PIO environment і **не** PIO board ID. Перелік значень з `flexible-arduino-2026.1.18-searchableOptions.jar` — це чипи ESP-IDF:

`esp32` · `esp32c2` · `esp32c3` · `esp32c5` · `esp32c6` · `esp32h2` · `esp32p4` · `esp32s2` · `esp32s3`

(Рядка `esp32dev` у JAR плагіна немає взагалі — попередня редакція цього документа помилково подавала PIO board ID як board targets плагіна.) Для backend'а `platformio` це поле у формуванні команди не бере участі; воно впливає на Pin Viewer і ESP-IDF/arduino-cli backend'и. У run config виставлено `esp32c6` — узгоджено з основним env.

**Ризик `esp8266` знято:** платформа `espressif8266` встановлена (`ls ~/.platformio/platforms/`), тож навіть повна збірка всіх env на ньому не впаде. Але з `default_envs` вона тепер і не запускається без явного `-e`.

**Побічний бонус `default_envs` — правильні опції монітора.** `monitorCommandLine` теж не передає `-e`, тому раніше `pio device monitor` брав опції не з `[env:esp32-c6]`. А там перевизначено `monitor_rts = 0` / `monitor_dtr = 0` (у `[env]` обидва `= 1`) саме через зависання монітора на C6 під Linux. Тепер завдяки `default_envs` монітор плагіна підхоплює ці значення — перевірено, у виводі видно:

```
--- forcing DTR inactive
--- forcing RTS inactive
```

Разом із цим підтягується і `monitor_filters = esp32_exception_decoder`: у логу видно `ROM ELF found … esp32c6_rev0_rom.elf` і `RISC-V GDB found for stack unwinding`, тобто backtrace з крашів декодуватиметься.

**Baud rate узгоджений по всьому ланцюжку:** `monitor_speed = 115200` у `[env]`, `Serial.begin(115200)` у `src/setup.h`, дефолт плагіна `serialBaudRate = 115200`. Нічого правити не треба.

**Перевірено на живій платі (25.08.2026):**

| Команда (як її формує плагін) | Результат |
| :--- | :--- |
| `pio run` | SUCCESS, 34.5 с, лише `esp32-c6` |
| `pio run --target upload --upload-port /dev/ttyACM0` | SUCCESS, 19.6 с, `Wrote 1784768 bytes`, `Hash of data verified` |
| `pio device monitor --port /dev/ttyACM0 --baud 115200` | працює, порт після виходу цілий і вільний |

**Пастка при перевірці монітора зі скрипта.** `pio device monitor` вимагає справжній TTY — miniterm робить `termios.tcgetattr(self.fd)` і без термінала падає з `termios.error: (25, 'Inappropriate ioctl for device')`. Це не проблема плагіна: в IDE монітор запускається в консольному вікні з PTY. Щоб перевірити з CLI, потрібен PTY:

```sh
script -qec "timeout -s INT -k 3 12 pio device monitor --port /dev/ttyACM0 --baud 115200" /dev/null
```

Для простого читання логів PTY не потрібен — досить pyserial **без торкання DTR/RTS**: у такому режимі чип не ресетиться (розділ 3.5). Саме так працює `./esp` у корені репозиторію.

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

| Комбінація | Дія | Що станеться з платою |
| :--- | :--- | :--- |
| `Ctrl+Alt+R` | Build Project | нічого — порт не відкривається |
| `Ctrl+Alt+U` | Build and Flash | заливка + ресет |
| `Ctrl+Alt+M` | Build, Flash and Monitor | заливка + ресет + відкритий порт |

Те саме для `Tools → Arduino`: `Flash to Device`, `Build and Flash`, `Build, Flash and Monitor`, `Upload Only (skip build)`, `OTA Upload`, `Open Serial Monitor`.

**Важливо після 3.5:** справді безпечний тут лише `Ctrl+Alt+R`. Будь-яка дія, що відкриває порт — включно з простим `Open Serial Monitor` — **перезапускає прошивку**. Це не залежить від налаштувань плагіна.

---

## 9. Безпечні команди

Перегенерувати `compile_commands.json` під конкретний env (плату не чіпає):

```sh
cd ~/Work/ESP32/esp32
~/.platformio/penv/bin/pio run -t compiledb -e esp32-c6
python3 tools/normalize_compdb.py     # ОБОВ'ЯЗКОВО, інакше макроси не резолвляться
```

**Другий крок не пропускати.** PlatformIO пише відносні шляхи, і плагін Compilation Database їх не мапить — файл виглядає як «не в проєкті», підсвітка йде без `-D`, і `#if defined(МАКРОС)` резолвиться як false. Розбір — у розділі 5.

`tools/normalize_compdb.py` робить абсолютними `file`, `output` і всі `-I`/`-isystem`; шляхи з пробілами квотує коректно. Ідемпотентний — повторний запуск нічого не псує.

`compiledb` не показується у `pio run --list-targets`, але є вбудованим таргетом PlatformIO 6. Фолбек: `pio run -t idedata -e <env>`.

Compilation database для скетча без компіляції та заливки (флаг наявний в `arduino-cli` 1.5.1):

```sh
arduino-cli compile --only-compilation-database \
  --fqbn esp32:esp32:esp32c6 --build-path /tmp/ino-cdb <шлях-до-скетча>
```

Визначення плати без відкриття порту — читає лише USB VID/PID, ресета не викликає:

```sh
arduino-cli board list      # VID:PID 303a:1001 -> лише «ESP32 Family Device»
udevadm info -q property -n /dev/ttyACM0 | grep -E 'ID_VENDOR_ID|ID_MODEL_ID|ID_SERIAL'
```

**Точний чип цим не визначити:** усі ESP32 з USB-Serial/JTAG показують той самий `303a:1001`, тому `arduino-cli` каже загальне «ESP32 Family Device». Для точного визначення потрібен esptool — а він **відкриває порт і ресетить плату**:

```sh
~/.platformio/penv/bin/python ~/.platformio/packages/tool-esptoolpy/esptool.py \
  --port /dev/ttyACM0 flash_id
```

Дає чип, ревізію, розмір flash, MAC. На нашій платі: `ESP32-C6FH8 (QFN32) revision v0.2`, 8 MB, USB-Serial/JTAG.

Перевірка, що IDE не тримає порт відкритим:

```sh
lsof /dev/ttyACM0     # має бути порожньо, окрім вашого робочого процесу
```

Перевірка, чи проєкт відкритий у запущеній IDE — перед правкою `.idea/*` вручну:

```sh
for pid in $(pgrep -f 'bin/idea$'); do
  echo "$pid: $(readlink /proc/$pid/fd/* 2>/dev/null | grep -c 'Work/ESP32/esp32')"
done
# 0 -> проєкт не завантажений, файли правити можна
```

### Команди, що чіпають плату

Не «безпечні», але тут — щоб не шукати. Усі три ресетять чип (3.5):

```sh
# заливка — рівно те, що робить run config «Arduino Flash»
~/.platformio/penv/bin/pio run --target upload --upload-port /dev/ttyACM0

# монітор — те, що робить «Arduino Monitor». Потрібен PTY, зі скрипта падає на termios
script -qec "~/.platformio/penv/bin/pio device monitor --port /dev/ttyACM0 --baud 115200" /dev/null

# просто прочитати лог N секунд без miniterm (ресет все одно буде)
~/.platformio/penv/bin/python -c "
import serial,time
s=serial.Serial(); s.port='/dev/ttyACM0'; s.baudrate=115200
s.dtr=False; s.rts=False; s.timeout=1; s.open()
t=time.time(); b=b''
while time.time()-t<8: b+=s.read(4096)
s.close(); print(b.decode('utf-8','replace'))"
```

---

## 10. Чекліст

| № | Крок | Стан |
| :--- | :--- | :--- |
| 1 | Відкрити проєкт саме в **IDEA 2026.2** (у 2026.1 плагіна немає) | ✅ підтверджено: плагін є лише в 2026.2, проєкт останній раз відкривався в IU-262.9437.185 |
| 2 | Перевірити **ліцензію/тріал**: Settings → Plugins → Flexible For Arduino | ✅ статус **`purchased`** — обмежень немає, Pro-функції відкриті |
| 3 | Run config: `BUILD_FLASH_MONITOR` → `BUILD` | ✅ зроблено, `buildTarget` → `esp32c6`, плюс додані окремі `Arduino Flash` і `Arduino Monitor` (3.1) |
| 4 | Вимкнути `Reset device on connect`, `Assert DTR on connect`, `Auto-reconnect`, `Detect boot loops` — і переконатись, що вони **явно** у XML як `false` | ✅ усі 5 опцій із дефолтом `true` присутні у файлі як `false` і **вижили перезапуск IDE**. Але див. 3.5: на C6 reset/DTR не є бар'єром |
| 5 | Порт лишити порожнім, поки п.4 не зроблений | ✅ був порожній до появи плати; тепер `ttyACM0`, вписаний вручну. `usbAutoDetect=false`, тож автопідхвату немає |
| 6 | `Enable MCP Server` не вмикати (містить `flash`, `erase_flash`) | ✅ вимкнений. Явний `mcpEnabled=false` IDE прибрала з файлу як «рівний дефолту» — це очікувано (3.3), поведінка та сама |
| 7 | Перевірити збірку | ✅ `default_envs = esp32-c6` додано; `pio run` → SUCCESS за 34.5 с |
| 8 | Перегенерувати `compile_commands.json`; перевірити, чи IDEA підхоплює compilation database | ⚠️ **лишився один крок.** CDB перегенерований під `esp32-c6` (589 записів); плагін `com.intellij.clion-compdb` встановлений. Не зроблено: `Load Compilation Database Project` — дія тільки в UI (розділ 5, 13.1) |
| 9 | Вмикати flash / monitor — лише коли плата вільна | ✅ перевірено на ESP32-C6FH8: flash SUCCESS + `Hash of data verified`, монітор працює, прошивка стартує. Побічно з'ясовано 3.5 |
| 10 | JTAG/OpenOCD + Memory Analyzer — окремим етапом (Pro) | ⏸️ не починалось; `openOcdPath` порожній. Ліцензія `purchased` цього не блокує, а C6 має вбудований USB-JTAG — адаптер не потрібен (13.2) |

---

## 11. Як повернутись до задачі

### 11.1. Умова старту

Чекліст закритий, крім однієї UI-дії (13.1) і не початого JTAG-етапу (13.2). Особливої умови старту більше немає — плата під'єднана, ліцензія `purchased`, збірка/flash/монітор перевірені.

Єдине, про що треба пам'ятати перед роботою з портом: **будь-яке відкриття монітора перезапускає прошивку** (3.5). Якщо на платі є стан, який не можна втрачати — монітор не відкривати.

### 11.2. Готовий промпт для Claude Code

Запустити Claude Code у теці цього проєкту й дати такий промпт:

```
Працюємо з плагіном "Flexible For Arduino" в IntelliJ IDEA.

Стан задокументований — прочитай спочатку
docs/flexible_for_arduino_setup.md: налаштування завершене
(журнал у розділі 12), лишились пункти розділу 13.

Перед будь-якими діями виконай блок ре-верифікації з розділу 11.3.
Якщо версія плагіна змінилась — перепровір факти за розділом 11.4.

Врахуй 3.5: на ESP32-C6 чип ресетить не відкриття порту, а зміна DTR/RTS,
незалежно від налаштувань. Якщо на платі є стан, який не можна
втрачати — попередь, перш ніж відкривати монітор.
```

Якщо плата зайнята або на ній є важливий стан — додати:

```
Плату НЕ ресетити: flash / monitor заборонені.
Роби тільки те, що не відкриває послідовний порт.
```

Щоб не давати цей промпт вручну щоразу, можна створити в корені проєкту `CLAUDE.md` з рядком-вказівником, і Claude Code підхопить його автоматично:

```markdown
## Налаштування IDE
Стан і застереження по плагіну «Flexible For Arduino» — у
`docs/flexible_for_arduino_setup.md`. Перед будь-якими діями з
плагіном, портом `/dev/ttyACM0` або run-конфігураціями прочитати цей файл.
```

### 11.3. Блок швидкої ре-верифікації

Перевіряє все критичне без повторної декомпіляції. Повністю read-only, порт не відкриває. Запускати з кореня проєкту:

```sh
IDE="$HOME/.local/share/JetBrains/IntelliJIdea2026.2"
JAR=$(ls "$IDE"/flexible-arduino/lib/flexible-arduino-*.jar 2>/dev/null | grep -v searchableOptions | head -1)

echo "плагін:   ${JAR:-НЕ ВСТАНОВЛЕНИЙ}"
echo "версія:   $(basename "${JAR:-none}" .jar | sed 's/flexible-arduino-//')"
echo "ліцензія: $(unzip -p "$JAR" META-INF/plugin.xml | grep -o '<product-descriptor[^/]*/>')"
echo "reference-контриб'ютори (0 = ctrl+click немає): \
$(unzip -p "$JAR" META-INF/plugin.xml \
  | grep -cE 'psi\.referenceContributor|gotoDeclarationHandler|referencesSearch')"

echo "--- режим run config (треба BUILD) ---"
grep -o 'mode="[A-Z_]*"' .idea/workspace.xml || echo "(ARDUINO_RUN не знайдено)"

echo "--- перевизначені небезпечні опції ---"
grep -E 'monitorResetOnConnect|monitorDtrOnConnect|monitorAutoReconnect|usbAutoDetect|comPort|serialPort' \
  .idea/arduino-settings.xml || echo "(нічого не перевизначено -> діють дефолти з розділу 3.2)"

echo "--- порт зайнятий? ---"
lsof /dev/ttyACM0 2>/dev/null || echo "порт вільний або відсутній"
```

Еталон станом на кінець 25.08.2026:

- версія `2026.1.18`, `code="PSCIPIOAR"`, reference-контриб'юторів `0`, ліцензія `purchased`
- три конфігурації: `mode="BUILD"`, `mode="BUILD_AND_FLASH"`, `mode="MONITOR"`. Якщо десь з'явився `BUILD_FLASH_MONITOR` — це регресія, повернути розділення з 3.1
- перевизначені опції: `comPort`/`serialPort` = `ttyACM0`; `monitorResetOnConnect`/`monitorDtrOnConnect`/`monitorAutoReconnect`/`usbAutoDetect`/`monitorBootLoopDetection` = `false`. Якщо когось із цих **п'яти** у файлі немає — діє дефолт `true`, тобто регресія (3.3). Відсутність `monitorRtsOnConnect`/`mcpEnabled` — норма, їхній дефолт і так `false`
- порт вільний (плагін не тримає його між сесіями)

Додати до блоку ще дві перевірки, яких у попередній редакції не було:

```sh
echo "--- режими run configs (треба BUILD + BUILD_AND_FLASH + MONITOR) ---"
grep -o 'mode="[A-Z_]*"' .idea/workspace.xml

echo "--- default_envs (треба esp32-c6) ---"
grep -A1 '^\[platformio\]' platformio.ini | grep default_envs || echo "(НЕМА -> pio run збиратиме всі 7 env)"

echo "--- плагін Compilation Database ---"
ls ~/.local/share/JetBrains/IntelliJIdea2026.2 | grep -i compdb \
  || echo "(НЕ встановлений -> ctrl+click по .cpp/.h не працюватиме)"

echo "--- проєкт залінкований з CDB? ---"
grep -c CompDBLocalSettings .idea/workspace.xml   # 0 = ні, треба Load Compilation Database Project

echo "--- під який env згенерований compile_commands.json ---"
python3 -c "
import json,re,collections
d=json.load(open('compile_commands.json'))
print(len(d),'записів;',dict(collections.Counter(
  m.group(1) for c in d if (m:=re.search(r'\.pio/build/([^/]+)/',c['command'])))))"

echo "--- який чип реально на порту (ресетне плату!) ---"
# ~/.platformio/penv/bin/python ~/.platformio/packages/tool-esptoolpy/esptool.py \
#   --port /dev/ttyACM0 flash_id
arduino-cli board list   # без ресету, але чип не розрізняє
```

### 11.4. Якщо версія плагіна змінилась — де брати факти заново

Усі твердження цього документа отримані з самого JAR. Першоджерело:

```sh
IDE="$HOME/.local/share/JetBrains/IntelliJIdea2026.2/flexible-arduino/lib"
JAR=$(ls "$IDE"/flexible-arduino-*.jar | grep -v searchableOptions | head -1)
```

| Що потрібно | Звідки |
| :--- | :--- |
| Фічі, дії, хоткеї, tool windows, чи з'явились reference-контриб'ютори | `unzip -p "$JAR" META-INF/plugin.xml` |
| Тип ліцензії | там само, тег `<product-descriptor>` |
| Тексти UI, перелік MCP-tools, рядки про ліцензію | `unzip -p "$JAR" messages/ArduinoBundle.properties` |
| Повний перелік полів сторінки налаштувань | `unzip -p "$IDE"/flexible-arduino-*-searchableOptions.jar search/*.searchableOptions.xml` |
| **Дефолти налаштувань** (розділ 3.2) | розпакувати `com/ilscipio/language/arduino/settings/ArduinoSettingsState.class`, далі:<br>`javap -p -c ArduinoSettingsState.class \| sed -n '/<init>/,/^  public/p' \| grep -E 'putfield\|iconst\|ldc\|sipush'` |
| Куди пишуться налаштування | `javap -v -p ArduinoSettingsState.class \| grep -A8 RuntimeVisibleAnnotations` (анотація `@State`/`@Storage`) |
| Які саме команди запускає backend | розпакувати `com/ilscipio/language/arduino/backend/PlatformIoBackend.class`, далі `strings` по ньому |
| Режими run config | `strings` по `com/ilscipio/language/arduino/run/ArduinoRunConfiguration*.class` |

Розпаковувати в тимчасову теку, не в проєкт:

```sh
mkdir -p /tmp/fa && cd /tmp/fa && unzip -o -q "$JAR" 'com/ilscipio/language/arduino/*' 'messages/*'
```

Стан IDE поза проєктом (перевіряти при зміні версії IDEA):

- чи плагін узагалі є в цій версії IDE — `ls ~/.local/share/JetBrains/IntelliJIdea*/flexible-arduino`
- чи увімкнений C/C++ плагін — `grep -c clion ~/.config/JetBrains/IntelliJIdea2026.2/disabled_plugins.txt`
- кому належить `.ino` — `grep ino ~/.config/JetBrains/IntelliJIdea2026.2/options/filetypes.xml`

### 11.5. Зовнішні джерела

- Marketplace: `https://plugins.jetbrains.com/plugin/31027-flexible-for-arduino`
- Метадані та історія версій через API (не потребує авторизації):
  `curl -s https://plugins.jetbrains.com/api/plugins/31027/updates?size=5`
- GitHub для issue та feature request: `https://github.com/ilscipio/flexible-for-arduino-jetbrains-plugin`
- Вендор: Ilscipio, `info@ilscipio.com`

Офіційної документації, окрім опису на Marketplace і README на GitHub, у плагіна немає — тому декомпіляція з 11.4 і є основним джерелом.

---

## 12. Журнал змін — 25 серпня 2026

### 12.1. Захід перший: без плати

Умови: плата **фізично відсутня** (жодного `/dev/ttyACM*`, `/dev/ttyUSB*`), проєкт **не відкритий** у запущеній IDEA (`readlink /proc/<pid>/fd/*` не дає жодного шляху в `Work/ESP32/esp32`) — тому файли `.idea/*` правились напряму без ризику перезапису.

Бекап оригіналів (`workspace.xml`, `arduino-settings.xml`, `platformio.ini`) — у скретчпаді сесії, поза проєктом.

#### Ре-верифікація: змін немає

Версія плагіна `2026.1.18`, `product-descriptor code="PSCIPIOAR"`, reference-контриб'юторів `0`. Усі факти документа, отримані з JAR, залишились чинні — повторна декомпіляція не знадобилась.

#### Зміни у файлах

| Файл | Було | Стало |
| :--- | :--- | :--- |
| `.idea/workspace.xml` | `mode="BUILD_FLASH_MONITOR"` | `mode="BUILD"` |
| `.idea/workspace.xml` | `buildTarget="esp32dev"` | `buildTarget="esp32c6"` |
| `.idea/arduino-settings.xml` | `comPort`/`serialPort` = `ttyACM1` | порожні |
| `.idea/arduino-settings.xml` | небезпечні опції відсутні (= дефолт `true`) | 7 опцій явно `false` (розділ 2) |
| `platformio.ini` | `[platformio]` без `default_envs` | `default_envs = esp32-c6` |
| `compile_commands.json` | 02.08, 427 записів, env `esp32-st7789` | 25.08, 589 записів, 25 МБ, env `esp32-c6` |

Основним env вибрано `esp32-c6` — за найсвіжішою роботою в git (`esp32-c6`, `esp32-c6 / flip`).

#### Перевірки

- `pio run` (без `-e`) → лише `esp32-c6`, **SUCCESS за 34.5 с** — саме те, що тепер робить `Ctrl+Alt+R`
- `pio run -t compiledb -e esp32-c6` → 6.7 с, `compile_commands.json` перезібраний
- `pio project config` → `default_envs: ['esp32-c6']` підхоплений
- обидва XML валідні (`xml.dom.minidom.parse`)

#### Виправлені помилки самого документа

- **Board targets.** Було подано `esp32dev`, `esp32-s3-devkitc-1`, `megaatmega2560`, `nanoatmega328`, `d1_mini` — це PIO board ID, а не значення поля `buildTarget`. Рядка `esp32dev` у JAR плагіна взагалі немає. Фактичний перелік (з `searchableOptions`) — чипи ESP-IDF: `esp32`, `esp32c2`, `esp32c3`, `esp32c5`, `esp32c6`, `esp32h2`, `esp32p4`, `esp32s2`, `esp32s3`.
- **Ризик `esp8266`.** Було «якщо платформа не встановлена, збірка впаде». Платформа `espressif8266` встановлена — ризику не було.
- **Compilation database в IDEA.** Було позначено як відкрите питання. Розв'язано: потрібен окремий безкоштовний плагін `com.intellij.clion-compdb` (розділ 5).

### 12.2. Захід другий: з платою на `/dev/ttyACM0`

Вхідні дані від користувача: ліцензія `purchased`, плагін `Compilation Database` встановлений, плата під'єднана.

#### Що підтвердилось

| Перевірка | Результат |
| :--- | :--- |
| Ліцензія | `purchased` — тріал не є обмеженням, Pro відкрито |
| Плагін compdb | `com.intellij.clion-compdb` 262.8665.176, встановлений |
| Чип на порту | **ESP32-C6FH8 (QFN32) rev v0.2**, 8 MB embedded flash, USB-Serial/JTAG, MAC `ac:eb:e6:2b:66:a4` |
| Збіг з env | повний: `board_upload.flash_size = 8MB` у `[env:esp32-c6]` відповідає реальним 8 MB |
| Захисні опції після перезапуску IDE | всі 5 із дефолтом `true` вижили; 2 з дефолтом `false` прибрані — як прогнозувалось у 3.3 |
| Baud rate | `monitor_speed=115200` = `Serial.begin(115200)` = дефолт плагіна |
| Flash | `pio run --target upload --upload-port /dev/ttyACM0` → SUCCESS 19.6 с, `Hash of data verified` |
| Прошивка після заливки | стартує: LittleFS змонтований, `EventDispatcher`/`ConfigStorage`/`SerialCommander` піднялись |
| Монітор | працює, `forcing DTR/RTS inactive` з env підхоплені, exception decoder активний, порт після виходу цілий |

(У логах видно `SD init fail` — SD-картки в слоті немає, вона в роботі по задачі `sd-rescue`. До налаштування IDE не стосується.)

#### Нове знання

- **розділ 3.5** — на ESP32-C6 ресет спричиняє зміна DTR/RTS, а не саме відкриття порту. `monitorResetOnConnect=false` усе одно не бар'єр (плагін виставляє лінії явно), але читати логи без ресету **можна** — pyserial без торкання цих ліній, як це робить `./esp`. **Уточнено 26.08.2026: попередня редакція цього розділу стверджувала протилежне і була хибною** — обидві «доказові» сесії виставляли DTR/RTS, тобто вимірювали не той сценарій;
- `default_envs` дає побічний ефект, якого не планували: `monitorCommandLine` теж не передає `-e`, тому тепер монітор плагіна підхоплює `monitor_rts=0`/`monitor_dtr=0` з `[env:esp32-c6]` (розділ 6);
- `pio device monitor` вимагає справжній TTY — зі скрипта падає на `termios.tcgetattr`. Перевіряти через `script -qec` (розділ 6);
- `CompDBLocalSettings` живе у `$WORKSPACE_FILE$`, тобто `.idea/workspace.xml` — звідси CLI-перевірка, чи проєкт залінкований з CDB (розділ 5);
- точні тексти дій compdb з його бандла: `Load Compilation Database Project`, `Select compile_commands.json`, `Reload Compilation Database Project`, `No Compilation Database project detected`.

#### Зміни у файлах

| Файл | Зміна |
| :--- | :--- |
| `.idea/arduino-settings.xml` | (правка користувача в UI) `comPort`/`serialPort` → `ttyACM0` |
| `.idea/workspace.xml` | додані run configs `Arduino Flash` (`BUILD_AND_FLASH`) і `Arduino Monitor` (`MONITOR`) + `<list>` з порядком |
| плата | перепрошита свіжою збіркою `esp32-c6` |

Розділення на три конфігурації — щоб `Ctrl+Alt+R` назавжди залишився таким, що не чіпає порт, а заливка вимагала окремого вибору. Конфігурації з `BUILD_FLASH_MONITOR` свідомо немає.

### 12.3. Захід третій: чому не резолвились макроси

Симптом від користувача: для `esp32-c6` у `build_flags` є `-D DISPLAY_SPLIT_COUNT=4`, а `#if defined(DISPLAY_SPLIT_COUNT) && DISPLAY_SPLIT_COUNT` у редакторі показується як невиконана умова.

#### Що виключили по дорозі

| Гіпотеза | Перевірка | Вердикт |
| :--- | :--- | :--- |
| Макросу немає в CDB | `-DDISPLAY_SPLIT_COUNT=4` у **589 з 589** записів | не вона |
| `src/Display.cpp` не потрапив у CDB | є (спершу здалось, що ні — шлях був відносним, а я шукав абсолютний) | не вона |
| CDB згенерований під інший env | усі записи `esp32-c6` | не вона |
| Компілятора немає на диску | `riscv32-esp-elf-g++` 14.2.0 на місці | не вона |
| Компілятор не приймає флаги з CDB | `-std=gnu++17`/`gnu++2a`/`gnu++2b` — усі три ОК (діє останній) | не вона |
| Макрос не доїжджає до препроцесора | `-dM -E` дає `#define DISPLAY_SPLIT_COUNT 4`; тестовий `#if` → активна гілка | не вона |
| Проєкт не залінкований з CDB | `CompDBLocalSettings` у `workspace.xml` уже був — користувач залінкував | не вона |

Тобто CDB був абсолютно правильний, і компілятор із його ж флагами резолвив макрос як 4.

#### Справжня причина

У `.idea/workspace.xml` знайшлось:

```xml
<component name="HighlightingSettingsPerFile">
  <setting file="file://$PROJECT_DIR$/src/Display.cpp" root0="FORCE_HIGHLIGHTING" />
  <setting file="file://$PROJECT_DIR$/src/main.cpp" root0="FORCE_HIGHLIGHTING" />
</component>
```

`FORCE_HIGHLIGHTING` — це наслідок натискання `Highlight anyway` на банері «файл не належить до проєкту». У цьому режимі C/C++ парситься **без флагів компіляції**, тобто без жодного `-D` — звідси й false у кожному `#if defined(...)`.

А банер з'явився тому, що PlatformIO пише в CDB **відносні шляхи**: 487 із 589 записів мали відносний `file`, усі 589 — відносний `output`, плюс 924 токени `-I`/`-isystem`. Формально валідно (резолвиться від `directory`), але плагін такі записи на файли редактора не мапить.

#### Фікс

1. створений `tools/normalize_compdb.py` — робить абсолютними `file`, `output`, `-I`/`-isystem`; шляхи з пробілами (`GFX Library for Arduino`) квотує коректно, ідемпотентний;
2. CDB перегенерований і нормалізований: відносних шляхів **0**;
3. з `workspace.xml` прибрані записи `FORCE_HIGHLIGHTING` для `.cpp`/`.h` — щоб файли підсвічувались із флагами, а не в обхід них;
4. перевірка: команда з нормалізованого CDB компілює `src/Display.cpp` через `-fsyntax-only` з **exit 0** і порожнім stderr.

Заодно з'ясувалось, що CDB-імпорт **замінив модель проєкту**: `.idea/modules.xml` і `.idea/esp32.iml` зникли, з'явились `CompDBSettings` і `CompDBWorkspace` у `misc.xml`. Старий `ProjectRootManager` з `project-jdk-type="ESP-IDF"` залишився, але резолв не блокує.

Лишилось перевірити в редакторі — розділ 13.1.

---

## 13. Що лишилось

### 13.1. Перевірити, що навігація ожила

Проєкт **уже залінкований** з compilation database (`CompDBLocalSettings` у `workspace.xml`, `CompDBSettings` у `misc.xml`), CDB нормалізований, сліди `FORCE_HIGHLIGHTING` для C++ прибрані. Лишилось переконатись у редакторі.

Порядок:

1. відкрити проєкт в IDEA 2026.2;
2. `Reload Compilation Database Project` — щоб індекс піднявся з нормалізованого CDB;
3. відкрити `src/Display.cpp` і подивитись на `#if defined(DISPLAY_SPLIT_COUNT) && DISPLAY_SPLIT_COUNT` (рядки 27, 48, 57, 80, 150). Код під ним має бути **активним**, не сірим;
4. ctrl+click по `TFT_eSprite` або `tft_` — має вести в реальний хедер;
5. якщо десь знову з'явиться банер «файл не належить до проєкту» — **не** натискати `Highlight anyway`: це вимкне флаги і замаскує проблему. Замість цього перевірити CDB командами з розділу 5.

Якщо в `src/Display.h` умова резолвиться інакше, ніж у `src/Display.cpp` — це вибір контексту хедера, а не макроси (хедерів у CDB немає за визначенням). Перемикач контексту — над редактором.

**Після кожної регенерації CDB** (зміна env, нові `lib_deps`, нові файли): `pio run -t compiledb -e <env>` → `python3 tools/normalize_compdb.py` → `Reload Compilation Database Project`.

### 13.2. JTAG-дебаг — окремим етапом

Не починалось. Тепер для цього немає перепон: ліцензія `purchased` (JTAG/GDB — Pro-функція), а ESP32-C6 має **вбудований USB-Serial/JTAG** — окремий адаптер не потрібен, дебаг по тому самому USB-кабелю.

Що робити: заповнити `openOcdPath` у Settings → Arduino/ESP-IDF (зараз порожній) і створити конфігурацію в режимі `DEBUG` (є в enum `RunMode`, розділ 3.1).

Побічно варто перевірити Memory Analyzer і Device Profiler — теж Pro, теж досі не запускались.

### 13.3. Чого перевірити не вийде без окремої плати

Усе, що стосується інших шести env (`esp32-4848s040`, `esp32-s3-lcd147`, `esp8266`, `esp32-st7789`, `ttgo-t1`, `esp32-c6-lcd096`). Зараз `default_envs = esp32-c6`, тому:

- збірка іншого env — `pio run -e <env>` у терміналі (з IDE не вибрати, розділ 6);
- переключення основного env — правити `default_envs` **і** перегенерувати `compile_commands.json` під новий env, інакше навігація показуватиме код не тієї плати;
- `buildTarget` у run configs теж варто переставити під новий чип (перелік — розділ 6).
