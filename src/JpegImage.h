#pragma once

#include <Arduino.h>

// Глибина кольору, з якою зберігається декодоване зображення в пам'яті
enum class JpegColorDepth : uint8_t
{
    RGB565 = 16, // 2 байти/піксель, повна якість кольору
    RGB332 = 8   // 1 байт/піксель, вдвічі менше пам'яті
};

class JpegImage
{
public:
    JpegImage();
    ~JpegImage();

    // depth визначає, скільки пам'яті займе фінальний буфер:
    // RGB565 -> width*height*2 байт
    // RGB332 -> width*height*1 байт
    bool loadFromLittleFS(const char *path, JpegColorDepth depth = JpegColorDepth::RGB565);

    bool isLoaded() const;
    uint16_t width() const;
    uint16_t height() const;
    JpegColorDepth colorDepth() const;
    size_t bufferSizeBytes() const;

    // Сирий вказівник без типізації (потрібен явний каст під конкретну глибину)
    void *buffer() const;

    // Типізовані геттери з перевіркою поточної глибини кольору.
    // Повертають nullptr, якщо зображення завантажено з іншою глибиною.
    const uint16_t *bufferRGB565() const;
    const uint8_t *bufferRGB332() const;

private:
    static bool jpegOutputCallback(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t *bitmap);
    void freeBuffer();

    // Конвертація одного пікселя RGB565 -> RGB332 (3-3-2 біт)
    static inline uint8_t rgb565to332(uint16_t c)
    {
        uint8_t r = (c >> 8) & 0xE0; // старші 3 біти червоного -> біти 7-5
        uint8_t g = (c >> 6) & 0x1C; // старші 3 біти зеленого  -> біти 4-2
        uint8_t b = (c >> 3) & 0x03; // старші 2 біти синього   -> біти 1-0
        return r | g | b;
    }

    void *_buffer; // uint16_t* для RGB565 або uint8_t* для RGB332
    uint16_t _width;
    uint16_t _height;
    JpegColorDepth _depth;
    bool _loaded;
    bool _usedPsram;

    static JpegImage *_activeInstance;
};