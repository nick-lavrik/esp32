from PIL import Image

img = Image.open("space-02.jpg").convert("RGB")
img = img.resize((320, 240))  # якщо треба

with open("background.h", "w") as f:
    f.write("#pragma once\n#include <pgmspace.h>\n\n")
    f.write(f"const uint16_t background_data[{320*240}] PROGMEM = {{\n")
    pixels = list(img.getdata())
    for i, (r, g, b) in enumerate(pixels):
        rgb565 = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
        f.write(f"0x{rgb565:04X},")
        if (i + 1) % 16 == 0:
            f.write("\n")
    f.write("};\n")
