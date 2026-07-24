// convert.c
// Компіляція: gcc convert.c -o convert -lm
// Використання: ./convert background.jpg background.h 320 240

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char** argv) {
    if (argc < 3) {
        fprintf(stderr, "Використання: %s input.jpg output.h [width] [height]\n", argv[0]);
        return 1;
    }

    const char* srcPath = argv[1];
    const char* outPath = argv[2];
    int targetW = argc > 3 ? atoi(argv[3]) : 0;
    int targetH = argc > 4 ? atoi(argv[4]) : 0;

    int w, h, channels;
    unsigned char* data = stbi_load(srcPath, &w, &h, &channels, 3); // force RGB
    if (!data) {
        fprintf(stderr, "Не вдалось відкрити %s\n", srcPath);
        return 1;
    }

    // stb_image не має ресайзу сам по собі — якщо треба інший розмір,
    // готуй картинку заздалегідь (наприклад через `convert` з imagemagick)
    if (targetW && targetH && (targetW != w || targetH != h)) {
        fprintf(stderr, "Розмір файлу %dx%d не збігається з заданим %dx%d.\n", w, h, targetW, targetH);
        fprintf(stderr, "Підготуй картинку потрібного розміру заздалегідь, напр.:\n");
        fprintf(stderr, "  convert background.jpg -resize %dx%d! background.jpg\n", targetW, targetH);
        stbi_image_free(data);
        return 1;
    }

    FILE* f = fopen(outPath, "w");
    if (!f) {
        fprintf(stderr, "Не вдалось створити %s\n", outPath);
        stbi_image_free(data);
        return 1;
    }

    fprintf(f, "#pragma once\n#include <pgmspace.h>\n\n");
    fprintf(f, "const uint16_t background_data[%d] PROGMEM = {\n", w * h);

    int count = 0;
    for (int i = 0; i < w * h; i++) {
        unsigned char r = data[i * 3 + 0];
        unsigned char g = data[i * 3 + 1];
        unsigned char b = data[i * 3 + 2];

        uint16_t rgb565 = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
        fprintf(f, "0x%04X,", rgb565);

        if (++count % 16 == 0) fprintf(f, "\n");
    }

    fprintf(f, "};\n");
    fclose(f);
    stbi_image_free(data);

    printf("Готово: %s (%d байт)\n", outPath, w * h * 2);
    return 0;
}