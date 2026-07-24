#pragma once

#include <Arduino.h>
// #include <TFT_eSPI.h>

// Клас для завантаження JPEG-файлу з LittleFS та декодування
// у буфер RGB565, готовий для TFT_eSprite::pushImage()
class JpegImage
{
public:
    JpegImage();
    ~JpegImage();

    // Завантажує та декодує jpg-файл з LittleFS
    bool loadFromLittleFS(const char *path);

    bool isLoaded() const;
    uint16_t width() const;
    uint16_t height() const;
    uint16_t *buffer() const;

    // Виводить зображення напряму через TFT_eSprite
    // void pushTo(TFT_eSprite &sprite, int32_t x = 0, int32_t y = 0) const;

private:
    static bool jpegOutputCallback(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t *bitmap);
    void freeBuffer();

    uint16_t *_buffer;
    uint16_t _width;
    uint16_t _height;
    bool _loaded;
    bool _usedPsram;

    // TJpg_Decoder працює через C-callback, тому потрібен вказівник
    // на "активний" екземпляр, з яким зараз відбувається декодування
    static JpegImage *_activeInstance;
};
