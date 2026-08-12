# Image Effects — довідник фільтрів

> Опис усіх ефектів над `JpegImage` (пер-піксельні у `lib/JpegImage/Pixel.hpp`,
> буфер-вайд у `lib/JpegImage/ImageEffects.{hpp,cpp}`). Для архітектурних рішень
> (чому ефекти окремо від `JpegImage`, чому in-place без буфера на весь кадр) —
> див. `docs/architecture.md`.

## Дві категорії

**Пер-піксельні (`Pixel::fx*`)** — не потребують сусідніх пікселів, працюють
над одним `Pixel` за раз. У `ImageEffects` викликаються через `applyPerPixel`
(без додаткової пам'яті, окрім одного `Pixel` на стеку).

**Буфер-вайд (`ImageEffects::apply*`)** — або залежать від позиції пікселя
(vignette, scanlines), або потребують сусідів (blur, sobel, emboss, chromatic
aberration). Використовують тимчасовий буфер лише в один рядок/стовпець
(`Pixel[width]` чи `Pixel[height]`, не `Pixel[width*height]`) — див.
"Пам'ять" у `architecture.md`.

Усі методи `ImageEffects::apply*` повертають `bool` (`false` — зображення не
завантажене, або глибина кольору `MONO1` не підтримується, або невалідні
параметри) і працюють **in-place**, змінюючи буфер переданого `JpegImage`.

---

## Пер-піксельні ефекти

### `fxDesaturate(float factor)` / `ImageEffects::applyDesaturate` — команда `desaturate`
Знебарвлення до відтінків сірого через luma (Rec.709: `0.2126R + 0.7152G + 0.0722B`).
- `factor`: `0.0` = повна сірість (grayscale), `1.0` = без змін.
- Приклад: `applyDesaturate(image, 0.3f)` / серійна команда `desaturate 0.3`.

### `fxLighten(float factor)` / `applyLighten` — команда `lighten`
Освітлення в бік білого.
- `factor`: `0.0` = без змін, `1.0` = у чисто білий.
- Команда: `lighten 0.4`.

### `fxDarken(float factor)` / `applyDarken` — команда `darken`
Затемнення в бік чорного (множення каналів).
- `factor`: `1.0` = без змін, `0.0` = у чорний.
- Команда: `darken 0.6`.

### `fxTint(const Pixel &tint, float alpha)` / `applyTint` — команда `tint`
Змішування з довільним кольором.
- `tint`: цільовий колір (напр. `Pixel::unpack((uint32_t)0xFF8800)`).
- `alpha`: `0.0` = лише оригінал, `1.0` = повністю колір `tint`.
- Команда: `tint FF8800 0.4` (`<RRGGBB hex> [alpha 0.0-1.0, default 0.5]`).

### `fxContrast(float contrast)` / `applyContrast` — команда `contrast`
Класична контрастність відносно середньої точки `0.5`.
- `contrast`: `1.0` = без змін, `>1.0` — вище (напр. `1.5`), `<1.0` — нижче.
- Команда: `contrast 1.5`.

### `fxSepia(float amount)` / `applySepia` — команда `sepia`
Класична фотографічна сепія (матриця, не просто grayscale + tint — див. окреме
пояснення нижче "Sepia vs Grayscale").
- `amount`: `0.0` = оригінал, `1.0` = повна сепія.
- Команда: `sepia 1.0`.

### `fxInvert()` / `applyInvert` — команда `invert`
Негатив (кожен канал `1.0 - channel`). Без параметрів.
- Команда: `invert`.

### `fxThreshold(float threshold)` / `applyThreshold` — команда `threshold`
Порогова бінаризація: увесь піксель стає або чорним, або білим за luma. На
відміну від `fxSolarize` — знищує колір повністю.
- `threshold`: `0.0..1.0`, типово `0.5`.
- Команда: `threshold 0.6` (без аргументу — типово `0.5`).

### `fxHueRotate(float angle)` / `applyHueRotate` — команда `hue`
Обертання кольорового тону (hue) навколо осі яскравості, матриця обертання в
RGB-просторі.
- `angle` (у коді, радіани): `0..2π`. `π` (≈3.14) — приблизно доповнювальні кольори.
- Команда приймає **градуси**, не радіани: `hue 180` (конвертація в код відбувається
  всередині обробника команди).

### `fxThermal()` / `applyThermal` — команда `thermal`
"Тепловізор" — відкидає оригінальний колір, фарбує лише за яскравістю: темне →
синє, середнє → зелено-жовте, яскраве → червоно-біле. Без параметрів.
- Команда: `thermal`.

### `fxGamma(float gamma)` / `applyGamma` — команда `gamma`
Гамма-корекція (`channel ^ (1/gamma)`), інструмент калібрування під конкретну
панель (LovyanGFX і TFT_eSPI по-різному передають яскравість).
- `gamma`: `1.0` = без змін, `>1.0` — світліше в напівтонах, `<1.0` — темніше.
- Команда: `gamma 1.4` — компенсація темної на вигляд SPI-панелі.

### `fxPosterize(int levels)` / `applyPosterize` — команда `posterize`
Квантування кожного каналу до `levels` рівнів — плаский "постер"-вигляд.
- `levels`: ціле, `>=2`. `2` — чистий Ч/Б-постер по кожному каналу, `4-8` —
  типовий діапазон для помітного, але не грубого ефекту.
- Банди, які лишає posterize, добре маскуються подальшим `dither`-командою
  (саме тому це найкраща демонстрація того, що dithering взагалі щось робить —
  на звичайному фото банди майже непомітні, після posterize — дуже помітні).
- Команда: `posterize 4` — потім `dither` для перевірки.

### `fxSolarize(float threshold)` / `applySolarize` — команда `solarize`
Класичний фотографічний solarize: кожен канал **окремо**, вище порогу,
інвертується (на відміну від `fxThreshold` — колір лишається).
- `threshold`: `0.0..1.0`, типово `0.5`.
- Команда: `solarize 0.6` (без аргументу — типово `0.5`).

### `fxDuotone(const Pixel &dark, const Pixel &light)` / `applyDuotone` — команда `duotone`
Інтерполяція між двома кольорами за яскравістю пікселя. `fxSepia` — окремий
випадок duotone з фіксованою теплою парою (коричневий/кремовий).
- `dark`/`light`: кольори для темних/світлих ділянок.
- Команда: `duotone 1a0033 ffcc88` (`<dark RRGGBB> <light RRGGBB>`, обидва
  обов'язкові, без значень за замовчуванням).

### `fxColorBalance(float rMul, float gMul, float bMul)` / `applyColorBalance` — команда `balance`
Незалежне множення каналів — теплий/холодний баланс без зміни яскравості чи
контрасту (на відміну від `fxTint`, не тягне зображення до одного кольору).
- `rMul`/`gMul`/`bMul`: `1.0` = без змін по каналу.
- Команда: `balance 1.1 1.0 0.9` (тепліше зображення).

---

## Буфер-вайд ефекти

### `applyDitheringRGB332` / `applyDitheringRGB565` / `applyDitheringRGB888` — команда `dither`
Впорядкований дизеринг (матриця Байєра 8×8) — маскує банди квантування.
Без параметрів; кожен метод перевіряє, що `image.colorDepth()` відповідає
назві, інакше `false`. Амплітуда шуму підібрана під розрядність:
`RGB332 = 0.14`, `RGB565 = 0.03`, `RGB888 = 0.004`.
- На RGB888 ефект практично непомітний (квантування вже немає, що маскувати).
  Метод існує заради єдиного інтерфейсу команд для всіх плат.
- На звичайних фото ефект RGB565 теж ледь помітний — банд там мало (JPEG-шум
  уже маскує градієнти). Найкраще видно комбінацію з `posterize` вище.
- Команда `dither` (без аргументів) сама визначає `spaceImage.colorDepth()` і
  викликає відповідний з трьох методів.

### `applyBoxBlur(uint8_t radius, uint8_t passes = 1)` — команда `blur`
Box blur у 2 проходи (горизонтальний + вертикальний), тимчасовий буфер лише в
один рядок/стовпець.
- `radius`: `1-8` пікселів.
- `passes`: `1-3`. `2-3` дає результат, візуально близький до Гауса (кожен
  наступний прохід згладжує "квадратність" box blur).
- Команда: `blur 4 2` (`<radius 1-8> [passes 1-3, default 1]`).

### `applyVignette(float strength)` — команда `vignette`
Радіальне затемнення від центру до кутів. Найдешевший з буфер-вайд ефектів —
без тимчасового буфера, кожен піксель незалежний.
- `strength`: `0.0` = без ефекту, `1.0` = повне затемнення в кутах.
- Команда: `vignette 0.6` — фокусує погляд у центр екрана, добре пасує на
  `spaceImage`-фонах.

### `applyPixelate(uint8_t blockSize)` — команда `pixelate`
Усереднення `blockSize × blockSize` пікселів в один колір ("mosaic").
- `blockSize`: `>=2`. Останній блок у рядку/стовпці — обрізаний до межі
  кадру, якщо ширина/висота не кратна `blockSize`.
- Команда: `pixelate 8` — навмисний "low-res" вигляд, добре на
  `esp8266`/128×64 OLED замість боротьби з обмеженою роздільністю.

### `applyScanlines(float darkenFactor)` — команда `scanlines`
Затемнення кожного парного рядка (`y % 2 == 1`) — ретро-CRT вигляд. Найдешевший
з ефектів із залежністю від позиції (без `sqrt`/`sin`, просто парність рядка).
- `darkenFactor`: `1.0` = без змін, `0.0` = парні рядки повністю чорні.
- Команда: `scanlines 0.5`.

### `applyChromaticAberration(uint8_t offsetPx)` — команда `chromatic`
Зсуває R-канал вліво, B-канал вправо на `offsetPx` пікселів (G лишається на
місці) — "глітч"/сканфай вигляд. Потребує тимчасовий рядковий буфер (як
`applyBoxBlur`), бо зсув читає пікселі, які інший потік запису вже міг
перезаписати.
- `offsetPx`: `>=1`. Значення `0` — невалідне, повертає `false`.
- Команда: `chromatic 3`.

### `applySobelEdges()` — команда `sobel`
Виявлення країв (оператор Собеля, 3×3, на каналі яскравості), результат — Ч/Б
"креслення". Використовує 3 рядкові буфери (`prev/cur/next`), що котяться вниз
по кадру — не потребує буфера на весь кадр. Межові пікселі — clamp-to-edge.
- Без параметрів. Потребує мінімум `3×3` пікселі, інакше `false`.
- Добре на монохромних дисплеях (`esp8266`, `ttgo-t1`) — вихід уже по суті Ч/Б.
- Команда: `sobel`.

### `applyEmboss(float strength = 1.0f)` — команда `emboss`
Рельєфне тиснення — класична emboss-матриця `[-2 -1 0; -1 1 1; 0 1 2]` на
каналі яскравості, результат зсунутий на `+0.5` в сірий діапазон. Та сама
3-рядкова техніка, що й `applySobelEdges`.
- `strength`: `1.0` = класична інтенсивність, вище/нижче — сильніший/слабший
  рельєф.
- Потребує мінімум `3×3` пікселі.
- Команда: `emboss` (без аргументу — `strength=1.0`) або `emboss 1.8`.

### `applyNoise(float amount)` — команда `noise`
Псевдо-плівковий шум (grain) — незалежний випадковий зсув кожного каналу
кожного пікселя через Arduino `random(min, max)`.
- `amount`: `0.0` = без змін, `1.0` = максимальний шум (±100% каналу).
- Команда: `noise 0.08` — легке зерно, не руйнує зображення.

---

## Sepia vs Grayscale — чому це не одне й те саме

`fxDesaturate(0.0f)` (grayscale) зводить `r=g=b=luma` — нуль кольорової
інформації. `fxSepia` не просто прибирає колір, а прогонить піксель через
фіксовану матрицю, яка змішує канали так, щоб результат зсунувся у теплі
коричневі тони (`B`-канал завжди слабший за `R` — звідси "зістарений"
вигляд, а не нейтральний сірий). `fxDuotone` — узагальнення того самого
принципу на довільну пару кольорів, `fxSepia` — окремий випадок `fxDuotone`
з фіксованою парою (темно-коричневий / кремовий).

---

## Команди `SerialCommander` (лише під `#if defined(LITTLEFS_BACKGROUND_IMAGE)`)

Усі команди працюють над глобальним `spaceImage` (`src/main.cpp`) і логують
результат через `Logger::info`. Список команд у порядку реєстрації:

| Команда | Приклад | Ефект |
|---|---|---|
| `blur <radius 1-8> [passes 1-3]` | `blur 4 2` | `applyBoxBlur` |
| `tint <RRGGBB hex> [alpha 0.0-1.0]` | `tint FF8800 0.4` | `applyTint` |
| `contrast <factor>` | `contrast 1.5` | `applyContrast` |
| `sepia <amount 0.0-1.0>` | `sepia 1.0` | `applySepia` |
| `desaturate <factor 0.0-1.0>` | `desaturate 0.3` | `applyDesaturate` |
| `darken <factor 0.0-1.0>` | `darken 0.6` | `applyDarken` |
| `lighten <factor 0.0-1.0>` | `lighten 0.4` | `applyLighten` |
| `invert` | `invert` | `applyInvert` |
| `threshold [level 0.0-1.0]` | `threshold 0.6` | `applyThreshold` |
| `hue <degrees 0-360>` | `hue 180` | `applyHueRotate` (градуси → радіани) |
| `thermal` | `thermal` | `applyThermal` |
| `gamma <value>` | `gamma 1.4` | `applyGamma` |
| `posterize <levels >=2>` | `posterize 4` | `applyPosterize` |
| `solarize [threshold 0.0-1.0]` | `solarize 0.6` | `applySolarize` |
| `duotone <dark RRGGBB> <light RRGGBB>` | `duotone 1a0033 ffcc88` | `applyDuotone` |
| `balance <rMul> <gMul> <bMul>` | `balance 1.1 1.0 0.9` | `applyColorBalance` |
| `noise <amount 0.0-1.0>` | `noise 0.08` | `applyNoise` |
| `vignette <strength 0.0-1.0>` | `vignette 0.6` | `applyVignette` |
| `pixelate <blockSize >=2>` | `pixelate 8` | `applyPixelate` |
| `scanlines <darkenFactor 0.0-1.0>` | `scanlines 0.5` | `applyScanlines` |
| `chromatic <offsetPx >=1>` | `chromatic 3` | `applyChromaticAberration` |
| `sobel` | `sobel` | `applySobelEdges` |
| `emboss [strength]` | `emboss 1.8` | `applyEmboss` |
| `dither` (без аргументів) | `dither` | `applyDitheringRGB332/565/888` за `colorDepth()` |
| `background <LittleFS path>` | `background /space.jpg` | перезавантажує `spaceImage` |
