#!/usr/bin/env bash
#
# analyze_firmware_size.sh
#
# Аналізує розмір ОКРЕМИХ об'єктних (.o) та архівних (.a) файлів
# у директорії збірки PlatformIO/ESP32, згрупованих по бібліотеці.
#
# ВАЖЛИВО: аналізуємо не фінальний .elf (він вже злінкований і не
# містить розбивки по файлах), а директорію .pio/build/<env>/,
# де лежать проміжні .o та .a файли.
#
# Використання:
#   ./analyze_firmware_size.sh [build_dir] [top_N]
#
# Приклад:
#   ./analyze_firmware_size.sh .pio/build/esp32-st7789 30

set -euo pipefail

BUILD_DIR="${1:-.pio/build/esp32-st7789}"
TOP_N="${2:-30}"

if [ ! -d "$BUILD_DIR" ]; then
    echo "Помилка: директорію збірки не знайдено: $BUILD_DIR"
    echo "Вкажіть правильний шлях, наприклад: .pio/build/esp32-st7789"
    exit 1
fi

SIZE_TOOL=""
if command -v xtensa-esp32-elf-size >/dev/null 2>&1; then
    SIZE_TOOL="xtensa-esp32-elf-size"
else
    CANDIDATE=$(find "$HOME/.platformio" -type f -name "xtensa-esp32-elf-size" 2>/dev/null | head -n1 || true)
    if [ -n "$CANDIDATE" ]; then
        SIZE_TOOL="$CANDIDATE"
    fi
fi

if [ -z "$SIZE_TOOL" ]; then
    echo "Помилка: не знайдено xtensa-esp32-elf-size."
    exit 1
fi

echo "Використовую: $SIZE_TOOL"
echo "Директорія збірки: $BUILD_DIR"
echo ""

TMP_RAW=$(mktemp)
trap 'rm -f "$TMP_RAW"' EXIT

# Збираємо всі .o та .a файли (архіви бібліотек типу libTFT_eSPI.a)
mapfile -t FILES < <(find "$BUILD_DIR" -type f \( -name "*.o" -o -name "*.a" \))

if [ "${#FILES[@]}" -eq 0 ]; then
    echo "Не знайдено жодного .o/.a файлу в $BUILD_DIR"
    echo "Переконайтесь, що проєкт було зібрано (pio run), і не очищено (clean)."
    exit 1
fi

echo "Знайдено ${#FILES[@]} файлів. Аналізую..."
echo ""

for f in "${FILES[@]}"; do
    {
        echo "###FILEPATH### $f"
        "$SIZE_TOOL" --format=sysv "$f" 2>/dev/null || true
    } >> "$TMP_RAW"
done

python3 -c "
import re
from collections import defaultdict

top_n = $TOP_N

with open('$TMP_RAW') as fh:
    lines = fh.read().splitlines()

entries = []  # (label, section, size)
current_toplevel = None
current_member = None

for line in lines:
    line = line.rstrip()
    if not line:
        continue

    m = re.match(r'^###FILEPATH### (.+)$', line)
    if m:
        current_toplevel = m.group(1)
        current_member = None
        continue

    # Заголовок члена архіву: 'libFoo.a(Bar.cpp.o)  :'  або просто 'Bar.cpp.o  :'
    m = re.match(r'^(.+?)\s*:$', line)
    if m and ('.o' in m.group(1) or '.a' in m.group(1)):
        current_member = m.group(1)
        continue

    # Рядок секції: '.text.foo    1234    0'  або '.rodata.str1.4   56   0'
    # ВАЖЛИВО: з -ffunction-sections/-fdata-sections назви секцій мають
    # суфікси (.text.myFunc, .rodata.str1.4), тому перевіряємо ПРЕФІКС,
    # а не точний збіг. .bss виключаємо свідомо — це RAM, не flash.
    m = re.match(r'^(\.\S+)\s+(\d+)\s+(\d+)', line)
    if m and current_toplevel:
        section, size = m.group(1), int(m.group(2))
        if section.startswith('.text') or section.startswith('.rodata') or section.startswith('.data'):
            label = current_member if current_member else current_toplevel
            entries.append((current_toplevel, label, section, size))

# Сума по кожному фізичному файлу (top-level .o/.a) незалежно від членів архіву
file_totals = defaultdict(int)
for toplevel, label, section, size in entries:
    file_totals[toplevel] += size

def guess_lib(path):
    parts = path.split('/')
    for i, p in enumerate(parts):
        # PlatformIO створює директорії типу 'lib650', 'lib752', 'lib7b4'
        # (lib + короткий хеш), а наступна частина шляху — реальна назва
        # бібліотеки, напр. 'lib650/ESP Mail Client/...'
        if re.match(r'^lib[0-9a-f]+$', p) and i + 1 < len(parts):
            return parts[i + 1]
        if p == 'libdeps' and i + 2 < len(parts):
            return parts[i + 2]
    if '/src/' in path or path.startswith('src/'):
        return '(ваш код: src/)'
    if 'FrameworkArduino' in path or path.endswith('libFrameworkArduino.a'):
        return '(Arduino framework)'
    return '(інше/невизначено)'

lib_totals = defaultdict(int)
for f, size in file_totals.items():
    lib_totals[guess_lib(f)] += size

print('=== Топ-{} файлів за розміром (.text+.rodata+.data, до лінкування) ==='.format(top_n))
print()
sorted_files = sorted(file_totals.items(), key=lambda x: -x[1])
for f, size in sorted_files[:top_n]:
    print(f'{size:>10} bytes  {f}')

print()
print('=== Згруповано по бібліотеці ===')
print()
sorted_libs = sorted(lib_totals.items(), key=lambda x: -x[1])
total = sum(lib_totals.values())
for lib, size in sorted_libs:
    pct = 100.0 * size / total if total else 0
    print(f'{size:>10} bytes  ({pct:5.1f}%)  {lib}')

print()
print(f'Загалом (.text+.rodata+.data по всіх .o/.a): {total} bytes')
print()
print('Примітка: ця сума може перевищувати фінальний розмір прошивки,')
print('бо лінкер відкидає невикористаний код (--gc-sections) та об\\'єднує')
print('дублікати. Це показує ПОТЕНЦІЙНИЙ внесок кожної бібліотеки,')
print('а не гарантовано те, що реально потрапило у фінальний .bin.')
"
