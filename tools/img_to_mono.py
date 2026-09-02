#!/usr/bin/env python3
"""PNG -> 1-бітний PROGMEM-масив у форматі MonoBitmap / Adafruit_GFX::drawBitmap().

Формат виводу: 1 біт/піксель, MSB = лівий піксель рядка, кожен рядок доповнений
до цілого байта (stride = ceil(width/8)) - той самий, що вже використовує
lib/JpegImage/MonoBitmap.hpp і MonoIcon16x16.

PNG-декодер тут власний, на stdlib (zlib + struct), без Pillow. На відміну від
data/convert.py, який жує повнокольорові фотографії 320x240, сюди приходять
плоскі спрайти в кілька десятків пікселів - тягнути заради них зовнішню
залежність не варто.

Використання:
    # окремий спрайт у header
    ./tools/img_to_mono.py --emit header -o assets/dino/trex.h trex.png

    # вирізати зі спрайтшита Chrome і подивитись ASCII
    ./tools/img_to_mono.py --emit ascii --crop 848,2,44,47 100-offline-sprite.png

    # у стилі MonoIcon16x16.cpp
    ./tools/img_to_mono.py --emit cpp --class DinoArt -o out.cpp a.png b.png
"""

import argparse
import os
import struct
import sys
import zlib


# --------------------------------------------------------------------------
# PNG
# --------------------------------------------------------------------------

class PngError(Exception):
    pass


_CHANNELS = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}


def _paeth(a, b, c):
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    return b if pb <= pc else c


def _unfilter(raw, height, stride, bpp):
    """Знімає per-scanline фільтри PNG (типи 0..4)."""
    out = bytearray()
    prev = bytearray(stride)
    pos = 0
    for row in range(height):
        if pos >= len(raw):
            raise PngError(f"дані обірвались на рядку {row}")
        ftype = raw[pos]
        pos += 1
        line = bytearray(raw[pos:pos + stride])
        if len(line) < stride:
            raise PngError(f"неповний рядок {row}")
        pos += stride

        if ftype == 0:
            pass
        elif ftype == 1:
            for i in range(bpp, stride):
                line[i] = (line[i] + line[i - bpp]) & 0xFF
        elif ftype == 2:
            for i in range(stride):
                line[i] = (line[i] + prev[i]) & 0xFF
        elif ftype == 3:
            for i in range(stride):
                left = line[i - bpp] if i >= bpp else 0
                line[i] = (line[i] + ((left + prev[i]) >> 1)) & 0xFF
        elif ftype == 4:
            for i in range(stride):
                left = line[i - bpp] if i >= bpp else 0
                upleft = prev[i - bpp] if i >= bpp else 0
                line[i] = (line[i] + _paeth(left, prev[i], upleft)) & 0xFF
        else:
            raise PngError(f"невідомий фільтр {ftype} у рядку {row}")

        out += line
        prev = line
    return out


def read_png(path):
    """-> (width, height, pixels), pixels[y][x] = (r, g, b, a), 8 біт на канал."""
    with open(path, "rb") as fh:
        data = fh.read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise PngError(f"{path}: не PNG")

    pos = 8
    hdr = None
    palette = b""
    trns = b""
    idat = bytearray()

    while pos < len(data):
        (length,) = struct.unpack(">I", data[pos:pos + 4])
        ctype = data[pos + 4:pos + 8]
        body = data[pos + 8:pos + 8 + length]
        pos += 12 + length  # 4 length + 4 type + payload + 4 crc

        if ctype == b"IHDR":
            hdr = struct.unpack(">IIBBBBB", body)
        elif ctype == b"PLTE":
            palette = body
        elif ctype == b"tRNS":
            trns = body
        elif ctype == b"IDAT":
            idat += body
        elif ctype == b"IEND":
            break

    if hdr is None:
        raise PngError(f"{path}: немає IHDR")
    width, height, depth, colour, comp, filt, interlace = hdr

    if interlace:
        raise PngError(f"{path}: interlaced PNG не підтримується — "
                       f"перезбережіть без Adam7 (напр. `convert in.png -interlace none out.png`)")
    if depth == 16:
        raise PngError(f"{path}: 16 біт на канал не підтримується — "
                       f"перезбережіть у 8-бітному (`convert in.png -depth 8 out.png`)")
    if colour not in _CHANNELS:
        raise PngError(f"{path}: невідомий colour type {colour}")

    nch = _CHANNELS[colour]
    stride = (width * nch * depth + 7) // 8
    bpp = max(1, (nch * depth) // 8)
    flat = _unfilter(zlib.decompress(bytes(idat)), height, stride, bpp)

    def samples(row):
        """Розпаковує один рядок у список семплів (уже 0..255 для depth<8)."""
        base = row * stride
        if depth == 8:
            return list(flat[base:base + stride])
        out, mask, scale = [], (1 << depth) - 1, 255 // ((1 << depth) - 1)
        for i in range(width * nch):
            bit = i * depth
            byte = flat[base + (bit >> 3)]
            shift = 8 - depth - (bit & 7)
            val = (byte >> shift) & mask
            out.append(val if colour == 3 else val * scale)
        return out

    pixels = []
    for y in range(height):
        srow = samples(y)
        line = []
        for x in range(width):
            if colour == 0:      # grayscale
                g = srow[x]
                line.append((g, g, g, 255))
            elif colour == 2:    # RGB
                i = x * 3
                line.append((srow[i], srow[i + 1], srow[i + 2], 255))
            elif colour == 3:    # palette
                idx = srow[x]
                if (idx + 1) * 3 > len(palette):
                    raise PngError(f"{path}: індекс {idx} поза палітрою")
                r, g, b = palette[idx * 3:idx * 3 + 3]
                a = trns[idx] if idx < len(trns) else 255
                line.append((r, g, b, a))
            elif colour == 4:    # grayscale + alpha
                i = x * 2
                g = srow[i]
                line.append((g, g, g, srow[i + 1]))
            else:                # RGBA
                i = x * 4
                line.append((srow[i], srow[i + 1], srow[i + 2], srow[i + 3]))
        pixels.append(line)
    return width, height, pixels


# --------------------------------------------------------------------------
# Пакування
# --------------------------------------------------------------------------

def pixels_to_bits(pixels, threshold, alpha_cut, invert):
    """-> список рядків із True/False. True = передній план (біт 1)."""
    rows = []
    for line in pixels:
        row = []
        for (r, g, b, a) in line:
            luma = (r * 299 + g * 587 + b * 114) // 1000
            on = (a >= alpha_cut) and (luma < threshold)
            row.append((not on) if invert else on)
        rows.append(row)
    return rows


def pack_rows(rows):
    """Біти -> байти. MSB = лівий піксель, рядок вирівняний до байта."""
    if not rows:
        return b"", 0, 0
    height = len(rows)
    width = len(rows[0])
    stride = (width + 7) // 8
    out = bytearray()
    for row in rows:
        for byte_i in range(stride):
            acc = 0
            for bit in range(8):
                x = byte_i * 8 + bit
                if x < width and row[x]:
                    acc |= 0x80 >> bit
            out.append(acc)
    return bytes(out), width, height


def unpack_rows(data, width, height):
    """Зворотне до pack_rows() — для round-trip перевірки."""
    stride = (width + 7) // 8
    rows = []
    for y in range(height):
        row = []
        for x in range(width):
            row.append(bool(data[y * stride + (x >> 3)] & (0x80 >> (x & 7))))
        rows.append(row)
    return rows


def rows_to_ascii(rows):
    return "\n".join("".join("#" if v else "." for v in row) for row in rows)


def ascii_to_rows(text, width=None, pad=True):
    """'#'/'.' -> біти. Порожні рядки ігноруються.

    pad=True доповнює короткі рядки фоном справа: малювати спрайт, вручну
    добиваючи кінцеві крапки до однакової довжини, надто легко зіпсувати —
    а різниця в один символ зсунула б увесь стовпець. Задане width при цьому
    лишається жорсткою межею: рядок довший за нього — це помилка, а не падинг.
    """
    rows = [ln.rstrip() for ln in text.strip().splitlines()]
    rows = [ln for ln in rows if ln]
    if not rows:
        raise ValueError("порожній ASCII-арт")

    for i, ln in enumerate(rows):
        bad = set(ln) - {"#", "."}
        if bad:
            raise ValueError(f"рядок {i + 1}: недопустимі символи {sorted(bad)} (можна лише '#' і '.')")

    longest = max(len(ln) for ln in rows)
    target = width if width is not None else longest
    if longest > target:
        raise ValueError(f"рядок завдовжки {longest} перевищує задану ширину {target}")
    if not pad:
        for i, ln in enumerate(rows):
            if len(ln) != target:
                raise ValueError(f"рядок {i + 1}: довжина {len(ln)}, очікувалось {target}")

    return [[c == "#" for c in ln.ljust(target, ".")] for ln in rows]


def format_bytes(data, stride, indent="  "):
    """Бінарні літерали + hex у коментарі — як у lib/JpegImage/MonoIcon16x16.cpp."""
    lines = []
    for i in range(0, len(data), stride):
        chunk = data[i:i + stride]
        bits = ", ".join(f"0b{b:08b}" for b in chunk)
        hexes = " ".join(f"0x{b:02X}," for b in chunk)
        lines.append(f"{indent}{bits}, // {hexes}")
    return "\n".join(lines)


def symbol_name(path):
    base = os.path.splitext(os.path.basename(path))[0]
    return "".join(ch if ch.isalnum() else "_" for ch in base).strip("_").lower()


# --------------------------------------------------------------------------
# CLI
# --------------------------------------------------------------------------

def parse_crop(text):
    parts = text.split(",")
    if len(parts) != 4:
        raise argparse.ArgumentTypeError("--crop очікує L,T,W,H")
    return tuple(int(p) for p in parts)


def load_bits(path, args):
    width, height, pixels = read_png(path)
    if args.crop:
        left, top, cw, ch = args.crop
        if left + cw > width or top + ch > height:
            raise PngError(f"{path}: --crop {args.crop} виходить за межі {width}x{height}")
        pixels = [row[left:left + cw] for row in pixels[top:top + ch]]
    return pixels_to_bits(pixels, args.threshold, args.alpha_cut, args.invert)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("png", nargs="+", help="вхідні PNG")
    ap.add_argument("--emit", choices=("header", "cpp", "ascii"), default="header")
    ap.add_argument("--name", help="ім'я символу (лише коли PNG один)")
    ap.add_argument("--class", dest="cls", default="MonoArt", help="клас для --emit cpp")
    ap.add_argument("--threshold", type=int, default=128, help="поріг яскравості (0..255)")
    ap.add_argument("--alpha-cut", type=int, default=128, help="поріг альфи (0..255)")
    ap.add_argument("--invert", action="store_true", help="перевернути передній/задній план")
    ap.add_argument("--crop", type=parse_crop, help="вирізати L,T,W,H")
    ap.add_argument("-o", "--out", help="файл виводу (типово stdout)")
    args = ap.parse_args(argv)

    if args.name and len(args.png) > 1:
        ap.error("--name можна лише з одним PNG")

    items = []
    for path in args.png:
        rows = load_bits(path, args)
        data, w, h = pack_rows(rows)
        items.append((args.name or symbol_name(path), data, w, h, rows))

    if args.emit == "ascii":
        chunks = []
        for name, _data, w, h, rows in items:
            chunks.append(f"# {name}  {w}x{h}\n{rows_to_ascii(rows)}")
        text = "\n\n".join(chunks) + "\n"
    elif args.emit == "header":
        out = ["#pragma once", "#include <pgmspace.h>", "", "// clang-format off"]
        for name, data, w, h in ((i[0], i[1], i[2], i[3]) for i in items):
            stride = (w + 7) // 8
            out += [f"#define {name.upper()}_W {w}",
                    f"#define {name.upper()}_H {h}",
                    f"const uint8_t {name}[{stride}u * {h}u] PROGMEM = {{",
                    format_bytes(data, stride),
                    "};", ""]
        out.append("// clang-format on")
        text = "\n".join(out) + "\n"
    else:  # cpp
        out = [f'#include "{args.cls}.hpp"', "", "// clang-format off"]
        for name, data, w, h in ((i[0], i[1], i[2], i[3]) for i in items):
            stride = (w + 7) // 8
            out += [f"// {w}x{h}, stride {stride} B",
                    f"const uint8_t {args.cls}::{name}[{len(data)}] PROGMEM = {{",
                    format_bytes(data, stride),
                    "};", ""]
        out.append("// clang-format on")
        text = "\n".join(out) + "\n"

    if args.out:
        with open(args.out, "w", encoding="utf-8") as fh:
            fh.write(text)
        print(f"{args.out}: {len(items)} sprite(s)", file=sys.stderr)
    else:
        sys.stdout.write(text)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (PngError, ValueError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        sys.exit(1)
