#!/usr/bin/env bash
#
# analyze_linked_size.sh
#
# На відміну від analyze_firmware_size.sh (який дивиться на .o ДО
# лінкування), цей скрипт парсить .map файл лінкера і показує
# РЕАЛЬНИЙ розмір кожної бібліотеки у фінальному, вже злінкованому
# бінарнику — тобто з урахуванням того, що --gc-sections відкинув
# невикористаний код.
#
# ПЕРЕДУМОВА: потрібен .map файл. Додайте в platformio.ini:
#
#   build_flags =
#       -Wl,-Map,firmware.map
#
# і перезберіть проєкт (pio run -e <env>).
#
# Використання:
#   ./analyze_linked_size.sh [шлях_до_firmware.map] [top_N]
#
# Приклад:
#   ./analyze_linked_size.sh .pio/build/esp32-st7789/firmware.map 30

set -euo pipefail

MAP_PATH="${1:-.pio/build/esp32-st7789/firmware.map}"
TOP_N="${2:-30}"

if [ ! -f "$MAP_PATH" ]; then
    echo "Помилка: map-файл не знайдено: $MAP_PATH"
    echo ""
    echo "Спочатку додайте в platformio.ini:"
    echo '  build_flags = -Wl,-Map,firmware.map'
    echo ""
    echo "Потім перезберіть: pio run -e <env>"
    echo ""
    echo "І знайдіть, де саме він з'явився:"
    echo "  find . -name 'firmware.map' 2>/dev/null"
    exit 1
fi

python3 -c "
import re
from collections import defaultdict

top_n = $TOP_N
map_path = '$MAP_PATH'

with open(map_path, errors='replace') as f:
    lines = f.readlines()

# Формат стандартного GNU ld map-файлу (розділ 'Linker script and memory map'):
#
#  .text.foo
#                  0x40080abc       0x64 .pio/build/esp32-st7789/src/main.cpp.o
#
# Секція може бути на тому ж рядку що й адреса/розмір, або на окремому.
# Обробляємо обидва варіанти.

entries = []  # (section, size, objpath)

# Знаходимо початок 'Linker script and memory map', щоб не чіпати
# верхню частину файлу (там інші таблиці символів/discarded input).
start_idx = 0
for i, l in enumerate(lines):
    if 'Linker script and memory map' in l:
        start_idx = i
        break

pending_section = None

line_re_full = re.compile(
    r'^\s*(\.\S+)\s+(0x[0-9a-fA-F]+)\s+(0x[0-9a-fA-F]+)\s+(\S.*\.o)\s*\$'
)
line_re_section_only = re.compile(r'^\s*(\.\S+)\s*\$')
line_re_addr_size_obj = re.compile(
    r'^\s+(0x[0-9a-fA-F]+)\s+(0x[0-9a-fA-F]+)\s+(\S.*\.o)\s*\$'
)

for line in lines[start_idx:]:
    line = line.rstrip('\n')
    if not line.strip():
        continue

    m = line_re_full.match(line)
    if m:
        section, addr, size, obj = m.group(1), m.group(2), m.group(3), m.group(4)
        entries.append((section, int(size, 16), obj))
        pending_section = None
        continue

    m = line_re_addr_size_obj.match(line)
    if m and pending_section:
        addr, size, obj = m.group(1), m.group(2), m.group(3)
        entries.append((pending_section, int(size, 16), obj))
        pending_section = None
        continue

    m = line_re_section_only.match(line)
    if m:
        pending_section = m.group(1)
        continue

if not entries:
    print('Не вдалося розпарсити map-файл. Перевірте формат вручну:')
    print(f'  less {map_path}')
    print('Шукайте розділ \"Linker script and memory map\".')
    raise SystemExit(1)

# Рахуємо тільки flash-релевантні секції (.text, .rodata, .data),
# ігноруємо .bss (RAM, не flash) та службові (.comment, .debug*, .xtensa.info)
file_totals = defaultdict(int)
for section, size, obj in entries:
    if section.startswith('.text') or section.startswith('.rodata') or section.startswith('.data'):
        file_totals[obj] += size

def guess_lib(path):
    parts = path.split('/')
    for i, p in enumerate(parts):
        if re.match(r'^lib[0-9a-f]+\$', p) and i + 1 < len(parts):
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

print(f'=== Топ-{top_n} файлів у ФІНАЛЬНІЙ прошивці (після gc-sections) ===')
print()
sorted_files = sorted(file_totals.items(), key=lambda x: -x[1])
for f, size in sorted_files[:top_n]:
    print(f'{size:>10} bytes  {f}')

print()
print('=== Згруповано по бібліотеці (РЕАЛЬНИЙ внесок у flash) ===')
print()
sorted_libs = sorted(lib_totals.items(), key=lambda x: -x[1])
total = sum(lib_totals.values())
for lib, size in sorted_libs:
    pct = 100.0 * size / total if total else 0
    print(f'{size:>10} bytes  ({pct:5.1f}%)  {lib}')

print()
print(f'Загалом (.text+.rodata+.data у фінальному бінарнику): {total} bytes')
print()
print('Це і є РЕАЛЬНИЙ розмір кожної бібліотеки у прошивці —')
print('точно те, що потрапило у .bin після відкидання мертвого коду.')
"
