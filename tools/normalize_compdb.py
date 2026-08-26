#!/usr/bin/env python3
"""Робить усі шляхи в compile_commands.json абсолютними.

PlatformIO пише відносні шляхи для всього, що лежить у корені проєкту
(`src/`, `lib/`, `.pio/libdeps/`), і абсолютні — для файлів фреймворку.
Формально це валідно: відносні шляхи резолвяться від поля `directory`.

Але JetBrains-плагін Compilation Database такі записи не мапить надійно:
файл виглядає як «не належить до проєкту», IDE пропонує «Highlight anyway»,
і в цьому режимі підсвітка працює БЕЗ -D з CDB — тобто `#if defined(МАКРОС)`
резолвиться як false, навіть коли в build_flags макрос заданий.

Нормалізує:
  * `file`   — шлях до translation unit
  * `output` — шлях до .o
  * `-I` / `-isystem` у `command`

Запускати після кожного `pio run -t compiledb`, далі в IDE —
`Reload Compilation Database Project`.
"""
import json
import os
import shlex
import sys

CDB = sys.argv[1] if len(sys.argv) > 1 else 'compile_commands.json'


def absolutize(path, base):
    return os.path.normpath(os.path.join(base, path))


with open(CDB, encoding='utf-8') as f:
    entries = json.load(f)

n_file = n_out = n_inc = 0
missing = []

for e in entries:
    base = e.get('directory', '')

    if not e['file'].startswith('/'):
        e['file'] = absolutize(e['file'], base)
        n_file += 1
    if not os.path.exists(e['file']):
        missing.append(e['file'])

    if 'output' in e and not e['output'].startswith('/'):
        e['output'] = absolutize(e['output'], base)
        n_out += 1

    toks = shlex.split(e['command'])
    out = []
    for t in toks:
        for flag in ('-I', '-isystem'):
            if t.startswith(flag) and len(t) > len(flag) and not t[len(flag):].startswith('/'):
                t = flag + absolutize(t[len(flag):], base)
                n_inc += 1
                break
        out.append(t)
    e['command'] = ' '.join(shlex.quote(t) for t in out)

with open(CDB, 'w', encoding='utf-8') as f:
    json.dump(entries, f, indent=2, ensure_ascii=False)

print(f'{CDB}: {len(entries)} записів')
print(f'  file -> абсолютних: {n_file}')
print(f'  output -> абсолютних: {n_out}')
print(f'  -I/-isystem -> абсолютних: {n_inc}')
if missing:
    print(f'  УВАГА: {len(missing)} шляхів не існує, напр.: {missing[:3]}')
