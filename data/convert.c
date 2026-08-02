// convert.c
// Компіляція: gcc convert.c -o convert -lm
// Використання:
//   RGB565 (кольоровий TFT):        ./convert background.jpg background.h 320 240
//   RGB565 з іменем масиву:         ./convert background.jpg background.h 320 240 backgroundSpace03
//   MONO1  (SSD1306, 1 біт/піксель): ./convert space.jpg space-mono-128x64.h 128 64 mono1 spaceMono128x64 [threshold=128]
//
// MONO1 формат сумісний з Adafruit_GFX::drawBitmap() і lib/MonoBitmap.hpp:
// 1 біт/піксель, MSB = лівий піксель рядка, рядок доповнений до цілого байта.

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void writeRgb565(FILE *f, const unsigned char *rgb, int w, int h, const char *arrayName)
{
    fprintf(f, "const uint16_t %s[%d] PROGMEM = {\n", arrayName, w * h);

    int count = 0;
    for (int i = 0; i < w * h; i++) {
        unsigned char r = rgb[i * 3 + 0];
        unsigned char g = rgb[i * 3 + 1];
        unsigned char b = rgb[i * 3 + 2];

        uint16_t rgb565 = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
        fprintf(f, "0x%04X,", rgb565);

        if (++count % 16 == 0) fprintf(f, "\n");
    }

    fprintf(f, "};\n");
}

// gray - один канал/піксель (0-255), як повертає stbi_load(..., 1)
static void writeMono1(FILE *f, const unsigned char *gray, int w, int h, const char *arrayName, int threshold)
{
    int rowBytes = (w + 7) / 8;

    fprintf(f, "const uint16_t %sWidth = %d;\n", arrayName, w);
    fprintf(f, "const uint16_t %sHeight = %d;\n", arrayName, h);
    fprintf(f, "const uint8_t %s[%d] PROGMEM = {\n", arrayName, rowBytes * h);

    unsigned char *row = (unsigned char *)malloc(rowBytes);
    int count = 0;

    for (int y = 0; y < h; y++) {
        memset(row, 0, rowBytes);
        for (int x = 0; x < w; x++) {
            if (gray[(size_t)y * w + x] >= threshold) {
                row[x / 8] |= 0x80 >> (x & 7);
            }
        }
        for (int i = 0; i < rowBytes; i++) {
            fprintf(f, "0x%02X,", row[i]);
            if (++count % 16 == 0) fprintf(f, "\n");
        }
    }

    fprintf(f, "};\n");
    free(row);
}

int main(int argc, char **argv)
{
    if (argc < 5) {
        fprintf(stderr, "Використання:\n");
        fprintf(stderr, "  RGB565: %s input.jpg output.h width height [array_name]\n", argv[0]);
        fprintf(stderr, "  MONO1:  %s input.jpg output.h width height mono1 array_name [threshold=128]\n", argv[0]);
        return 1;
    }

    const char *srcPath = argv[1];
    const char *outPath = argv[2];
    int targetW = atoi(argv[3]);
    int targetH = atoi(argv[4]);

    int mono = (argc > 5 && strcmp(argv[5], "mono1") == 0);
    const char *arrayName;
    int threshold = 128;

    if (mono) {
        if (argc < 7) {
            fprintf(stderr, "Для mono1 треба вказати ім'я масиву: %s in.jpg out.h W H mono1 array_name [threshold]\n", argv[0]);
            return 1;
        }
        arrayName = argv[6];
        if (argc > 7) threshold = atoi(argv[7]);
    } else {
        arrayName = (argc > 5) ? argv[5] : "background_data";
    }

    int w, h, srcChannels;
    int desiredChannels = mono ? 1 : 3; // stb_image сам конвертує в grayscale/RGB
    unsigned char *data = stbi_load(srcPath, &w, &h, &srcChannels, desiredChannels);
    if (!data) {
        fprintf(stderr, "Не вдалось відкрити %s\n", srcPath);
        return 1;
    }

    // stb_image не має ресайзу сам по собі — якщо треба інший розмір,
    // готуй картинку заздалегідь (наприклад через `convert` з imagemagick)
    if (targetW && targetH && (targetW != w || targetH != h)) {
        fprintf(stderr, "Розмір файлу %dx%d не збігається з заданим %dx%d.\n", w, h, targetW, targetH);
        fprintf(stderr, "Підготуй картинку потрібного розміру заздалегідь, напр.:\n");
        fprintf(stderr, "  convert %s -resize %dx%d! %s\n", srcPath, targetW, targetH, srcPath);
        stbi_image_free(data);
        return 1;
    }

    FILE *f = fopen(outPath, "w");
    if (!f) {
        fprintf(stderr, "Не вдалось створити %s\n", outPath);
        stbi_image_free(data);
        return 1;
    }

    fprintf(f, "#pragma once\n#include <pgmspace.h>\n\n");

    if (mono) {
        writeMono1(f, data, w, h, arrayName, threshold);
    } else {
        writeRgb565(f, data, w, h, arrayName);
    }

    fclose(f);
    stbi_image_free(data);

    printf("Готово: %s (%s)\n", outPath, mono ? "mono1" : "rgb565");
    return 0;
}
