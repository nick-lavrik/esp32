#!/usr/bin/env python3
"""Вшиває статику веб-порталу у прошивку.

Бере файли з data/www/ і генерує lib/WebPortal/WebPortalAssets.hpp - масиви
в PROGMEM, які віддає ProgmemStaticSource як ОСТАННЄ джерело статики.

Навіщо дубль того, що й так лежить у LittleFS: розділ буває порожнім (чиста
плата) або щойно стертим - 'pio run -t uploadfs' переписує його цілком. Без
вшитої копії пристрій, який не бачить жодної відомої мережі, не можна було б
налаштувати навіть з його власної точки доступу: портал віддавав би 404.

Вміст стискається gzip - браузер розпакує сам (Content-Encoding: gzip),
пристрій не розпаковує нічого. Сторінка на ~17 КБ лягає приблизно в 4 КБ
флеша.

Запуск (після кожної правки data/www/):
    ./tools/gen_web_assets.py
Згенерований заголовок комітиться разом із джерелом - збірка не залежить від
наявності Python.
"""

import gzip
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
SRC_DIR = ROOT / "data" / "www"
OUT = ROOT / "lib" / "WebPortal" / "WebPortalAssets.hpp"

# Що саме вшивати. Не весь каталог: у LittleFS-версію можна докладати важкі
# файли (шрифти, картинки), яким у прошивці робити нічого.
FILES = [
    ("index.html", "/index.html", "text/html; charset=utf-8"),
]

CONTENT_TYPES_HINT = "\n".join(f"//   {src} -> {path}" for src, path, _ in FILES)


def c_identifier(path: str) -> str:
    return "k" + "".join(part.capitalize() for part in path.strip("/").replace(".", "_").split("_"))


def emit_array(name: str, data: bytes) -> str:
    lines = [f"inline const uint8_t {name}[] PROGMEM = {{"]
    for offset in range(0, len(data), 16):
        chunk = data[offset : offset + 16]
        lines.append("    " + " ".join(f"0x{b:02x}," for b in chunk))
    lines.append("};")
    return "\n".join(lines)


def main() -> int:
    if not SRC_DIR.is_dir():
        print(f"no source directory: {SRC_DIR}", file=sys.stderr)
        return 1

    arrays, entries, report = [], [], []
    for source, path, content_type in FILES:
        raw = (SRC_DIR / source).read_bytes()
        packed = gzip.compress(raw, 9)
        name = c_identifier(path)

        arrays.append(emit_array(name, packed))
        entries.append(
            f'    {{"{path}", {name}, sizeof({name}), "{content_type}", true}},'
        )
        report.append(f"{source}: {len(raw)} -> {len(packed)} bytes gzipped")

    header = f"""#pragma once

// ЗГЕНЕРОВАНО tools/gen_web_assets.py - РУКАМИ НЕ ПРАВИТИ.
// Джерело: data/www/, перегенерувати: ./tools/gen_web_assets.py
//
// Вшита копія статики порталу (gzip). Використовується як останнє джерело в
// ланцюжку CompositeStaticSource, коли в LittleFS сторінки немає - див.
// WebPortal.hpp. Оновлювана версія тих самих файлів лежить у LittleFS
// ('pio run -t uploadfs') і має вищий пріоритет.
//
{CONTENT_TYPES_HINT}

#include <Arduino.h>
#include <ProgmemStaticSource.hpp>

namespace webassets {{

{chr(10).join(arrays)}

inline const ProgmemAsset kAssets[] = {{
{chr(10).join(entries)}
}};

inline constexpr size_t kAssetCount = sizeof(kAssets) / sizeof(kAssets[0]);

}}  // namespace webassets
"""

    OUT.write_text(header)
    print(f"{OUT.relative_to(ROOT)} written")
    for line in report:
        print("  " + line)
    return 0


if __name__ == "__main__":
    sys.exit(main())
