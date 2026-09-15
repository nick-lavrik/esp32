#!/usr/bin/env python3
"""Вшиває статику веб-порталу у прошивку.

Бере файли з assets/www/ і генерує lib/WebPortal/WebPortalAssets.hpp - масиви
в PROGMEM, які віддає ProgmemStaticSource.

Чому джерело лежить в assets/, а не в data/: усе, що в data/, потрапляє в
образ LittleFS і вимагає 'pio run -t uploadfs' на кожну правку - тобто окрему
прошивку окремого розділу, яку легко забути (і тоді свіжа сторінка мовчки не
видно, бо LittleFS має вищий пріоритет). Сторінка порталу цього не потребує:
вона їде всередині прошивки, разом із кодом, одним 'upload'.

LittleFS-джерело при цьому нікуди не зникло - воно лишається способом
підкласти КАСТОМНУ статику поверх вшитої (див. data/www/.readme.md). Просто
типовий шлях тепер не такий.

Вміст стискається gzip - браузер розпакує сам (Content-Encoding: gzip),
пристрій не розпаковує нічого. Сторінка на ~72 КБ лягає приблизно в 22 КБ
флеша.

Запуск (після кожної правки assets/www/):
    ./tools/gen_web_assets.py
Згенерований заголовок комітиться разом із джерелом - збірка не залежить від
наявності Python.
"""

import gzip
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
SRC_DIR = ROOT / "assets" / "www"
OUT = ROOT / "lib" / "WebPortal" / "WebPortalAssets.hpp"

# Що саме вшивати. Не весь каталог: у LittleFS поверх можна докласти важкі
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
// Джерело: assets/www/, перегенерувати: ./tools/gen_web_assets.py
//
// Вшита копія статики порталу (gzip) - ЗВИЧАЙНЕ джерело сторінки: вона їде
// всередині прошивки і оновлюється разом із нею, без 'uploadfs'. У ланцюжку
// CompositeStaticSource стоїть останньою (пріоритет -100), бо LittleFS /www
// лишається способом підкласти кастомну статику ПОВЕРХ неї - див.
// WebPortal.hpp і data/www/.readme.md.
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
