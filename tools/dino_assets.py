#!/usr/bin/env python3
"""Генерує src/Dino/DinoSprites.cpp — спрайти гри Chrome Dino.

Спрайти описані ПРЯМОКУТНИМИ БЛОКАМИ, а не ASCII-полотном. Причини дві:
  * Chrome Dino і в оригіналі складається з прямокутників — це його природна
    форма запису, і правка «підняти лапу на 2 px» тут виглядає як зміна одного
    числа, а не як перемальовування сорока рядків крапок;
  * обидва набори розмірів (tier M 44x47 і tier S ~22x24) виходять з ОДНОГО
    опису через scale — інакше довелося б малювати кожен спрайт двічі й
    стежити, щоб вони не розїхались.

Формат виводу — 1 біт/піксель, MSB зліва, stride = ceil(w/8): той самий, що
розуміють MonoBitmap, TFT_eSPI::drawBitmap і Arduino_GFX::drawBitmap.

Використання:
    ./tools/dino_assets.py                       # переписати DinoSprites.cpp
    ./tools/dino_assets.py --check               # лише звірити (для CI): 1 = застаріло
    ./tools/dino_assets.py --preview trexRunA    # подивитись ASCII
    ./tools/dino_assets.py --preview all --tier S
"""

import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from img_to_mono import format_bytes, pack_rows, rows_to_ascii  # noqa: E402

# --------------------------------------------------------------------------
# Опис у координатах tier M (44x47 для діно). (x, y, w, h)
# --------------------------------------------------------------------------

TREX_W, TREX_H = 44, 47

_TREX_BODY = [
    (27,  0, 17, 12),   # голова
    (27, 12,  9,  9),   # шия
    (36, 12,  8,  3),   # нижня щелепа, морда виступає вправо
    (13, 21, 24, 16),   # тулуб
    (0,  21, 15,  7),   # хвіст стирчить вліво на рівні верху тулуба
    (35, 27,  6,  3),   # передня лапка
    (13, 37, 22,  3),   # таз
]
_TREX_CUT = [
    (31,  4,  4,  4),   # око
    (39,  8,  5,  4),   # виїмка під мордою
    (0,  21,  5,  2),   # скіс кінчика хвоста
    (0,  26,  7,  2),
]
# Ноги: біг — це поперемінно піднята задня/передня. Стоїть — обидві опущені.
_LEGS_STAND = [(15, 40, 7, 7), (27, 40, 8, 7)]
_LEGS_A     = [(15, 40, 7, 7), (27, 40, 8, 4)]
_LEGS_B     = [(15, 40, 7, 4), (27, 40, 8, 7)]

# Мертвий: око «заплющене» (суцільний блок замість вирізу) і відкрита паща.
_TREX_DEAD_CUT = [c for c in _TREX_CUT if c != (31, 4, 4, 4)]
_TREX_DEAD_ADD = [(30, 12, 12, 4)]

SPRITES = {
    "trexIdle": dict(w=TREX_W, h=TREX_H, add=_TREX_BODY + _LEGS_STAND, cut=_TREX_CUT),
    "trexRunA": dict(w=TREX_W, h=TREX_H, add=_TREX_BODY + _LEGS_A, cut=_TREX_CUT),
    "trexRunB": dict(w=TREX_W, h=TREX_H, add=_TREX_BODY + _LEGS_B, cut=_TREX_CUT),
    "trexDead": dict(w=TREX_W, h=TREX_H,
                     add=_TREX_BODY + _LEGS_STAND + _TREX_DEAD_ADD, cut=_TREX_DEAD_CUT),

    # Кактус малий 17x35. Рука = вертикальна частина зовні + горизонтальна
    # перемичка ДО стовбура: без перемички рука висить у повітрі окремим
    # прямокутником (перевірено --preview).
    "cactusSmall": dict(w=17, h=35, add=[
        (6,  0,  5, 35),                     # стовбур
        (1, 11,  4,  7), (1, 15,  6,  3),    # ліва рука + перемичка
        (12, 7,  4,  8), (10, 12, 6,  3),    # права рука + перемичка
    ], cut=[]),

    # Кактус великий 25x50
    "cactusLarge": dict(w=25, h=50, add=[
        (9,  0,  7, 50),                     # стовбур
        (1, 16,  5, 10), (1, 22,  9,  4),    # ліва рука + перемичка
        (19, 9,  5, 12), (15, 17, 9,  4),    # права рука + перемичка
    ], cut=[]),

    # Хмара 46x14 — три «клуби» різної висоти
    "cloud": dict(w=46, h=14, add=[
        (0,  8, 46,  6),
        (8,  4, 30,  6),
        (16, 0, 16,  6),
    ], cut=[(0, 8, 3, 2), (43, 8, 3, 2)]),

    # Камінці землі
    "pebbleA": dict(w=4, h=2, add=[(0, 0, 4, 1), (1, 1, 2, 1)], cut=[]),
    "pebbleB": dict(w=3, h=2, add=[(0, 1, 3, 1), (1, 0, 1, 1)], cut=[]),
}

# Порядок у згенерованому файлі — стабільний, щоб diff не стрибав
ORDER = ["trexIdle", "trexRunA", "trexRunB", "trexDead",
         "cactusSmall", "cactusLarge", "cloud", "pebbleA", "pebbleB"]

TIERS = {"M": 1.0, "S": 0.5}


def _scale_blocks(blocks, k):
    out = []
    for (x, y, w, h) in blocks:
        sx, sy = int(round(x * k)), int(round(y * k))
        # Ширина/висота через праву межу, а не round(w*k): інакше сусідні блоки
        # після масштабування розходяться на піксель і в спрайті з'являються щілини.
        sw = max(1, int(round((x + w) * k)) - sx)
        sh = max(1, int(round((y + h) * k)) - sy)
        out.append((sx, sy, sw, sh))
    return out


def build(name, tier):
    spec = SPRITES[name]
    k = TIERS[tier]
    w = max(1, int(round(spec["w"] * k)))
    h = max(1, int(round(spec["h"] * k)))
    grid = [[False] * w for _ in range(h)]

    for (x, y, bw, bh) in _scale_blocks(spec["add"], k):
        for yy in range(y, min(y + bh, h)):
            for xx in range(x, min(x + bw, w)):
                if xx >= 0 and yy >= 0:
                    grid[yy][xx] = True
    for (x, y, bw, bh) in _scale_blocks(spec["cut"], k):
        for yy in range(y, min(y + bh, h)):
            for xx in range(x, min(x + bw, w)):
                if xx >= 0 and yy >= 0:
                    grid[yy][xx] = False
    return grid


HEADER = """// AUTOGENERATED by tools/dino_assets.py - DO NOT EDIT BY HAND.
// Щоб змінити спрайт, правте блоки в tools/dino_assets.py і перегенеруйте.
//
// Формат - 1 біт/піксель, MSB = лівий піксель рядка, рядок доповнений до
// цілого байта (той самий, що в lib/JpegImage/MonoBitmap.hpp).

#include "DinoSprites.h"

// clang-format off
"""


def generate():
    out = [HEADER]
    for tier in ("M", "S"):
        out.append(f"#if DINO_ASSET_TIER == {2 if tier == 'M' else 1}\n")
        for name in ORDER:
            grid = build(name, tier)
            data, w, h = pack_rows(grid)
            stride = (w + 7) // 8
            out.append(f"// {name}: {w}x{h}, stride {stride} B, {len(data)} B total")
            out.append(f"static const uint8_t k{name[0].upper()}{name[1:]}Bits[{len(data)}] PROGMEM = {{")
            out.append(format_bytes(data, stride))
            out.append("};")
            out.append("")
        out.append("#endif\n")

    out.append("// clang-format on\n")
    out.append("namespace DinoArt {")
    for name in ORDER:
        sym = f"k{name[0].upper()}{name[1:]}Bits"
        gm = build(name, "M")
        gs = build(name, "S")
        out.append(f"const MonoBitmap &{name}() {{")
        out.append(f"#if DINO_ASSET_TIER == 2")
        out.append(f"  static const MonoBitmap bmp({sym}, {len(gm[0])}, {len(gm)});")
        out.append(f"#else")
        out.append(f"  static const MonoBitmap bmp({sym}, {len(gs[0])}, {len(gs)});")
        out.append(f"#endif")
        out.append("  return bmp;")
        out.append("}")
    out.append("}  // namespace DinoArt")
    return "\n".join(out) + "\n"


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", default="src/Dino/DinoSprites.cpp")
    ap.add_argument("--check", action="store_true",
                    help="лише звірити з файлом; код 1 якщо згенероване відрізняється")
    ap.add_argument("--preview", metavar="NAME", help="надрукувати ASCII (ім'я або 'all')")
    ap.add_argument("--tier", choices=("M", "S"), default="M")
    args = ap.parse_args(argv)

    if args.preview:
        names = ORDER if args.preview == "all" else [args.preview]
        for name in names:
            if name not in SPRITES:
                print(f"error: невідомий спрайт {name!r}; є: {', '.join(ORDER)}", file=sys.stderr)
                return 1
            grid = build(name, args.tier)
            print(f"=== {name} (tier {args.tier}) {len(grid[0])}x{len(grid)} ===")
            print(rows_to_ascii(grid))
            print()
        return 0

    text = generate()
    if args.check:
        try:
            current = open(args.out, encoding="utf-8").read()
        except FileNotFoundError:
            print(f"{args.out}: немає файлу — треба згенерувати", file=sys.stderr)
            return 1
        if current != text:
            print(f"{args.out}: застаріло — перезапустіть tools/dino_assets.py", file=sys.stderr)
            return 1
        print(f"{args.out}: актуально")
        return 0

    os.makedirs(os.path.dirname(args.out), exist_ok=True)
    with open(args.out, "w", encoding="utf-8") as fh:
        fh.write(text)

    total_m = sum(len(pack_rows(build(n, "M"))[0]) for n in ORDER)
    total_s = sum(len(pack_rows(build(n, "S"))[0]) for n in ORDER)
    print(f"{args.out}: {len(ORDER)} спрайтів, tier M {total_m} B, tier S {total_s} B",
          file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
