#pragma once

#include <Arduino.h>
#include <TLogger.hpp>

// Глибина кольору, з якою зберігається декодоване зображення в пам'яті
enum class JpegColorDepth : uint8_t
{
    RGB565 = 16, // 2 байти/піксель, повна якість кольору, 16bit
    RGB332 = 8,  // 1 байт/піксель, вдвічі менше пам'яті
    MONO1  = 1   // 1 біт/піксель (поріг яскравості), формат Adafruit_GFX::drawBitmap
};


class JpegImage
{

public:
    JpegImage();
    ~JpegImage();

    // depth визначає, скільки пам'яті займе фінальний буфер:
    // RGB565 -> width*height*2 байт
    // RGB332 -> width*height*1 байт
    // MONO1  -> rowStrideBytes()*height (з округленням рядка до байта)
    bool loadFromLittleFS(const char *path, JpegColorDepth depth = JpegColorDepth::RGB565);

    // Поріг яскравості (0-255) для конвертації в MONO1: gray >= threshold -> білий піксель.
    // Викликати ДО loadFromLittleFS(..., JpegColorDepth::MONO1).
    void setMonoThreshold(uint8_t threshold);

    bool isLoaded() const;
    uint16_t width() const;
    uint16_t height() const;
    JpegColorDepth colorDepth() const;
    size_t bufferSizeBytes() const;

    // Довжина одного рядка в байтах для MONO1 (ceil(width/8)); 0 для інших глибин.
    size_t rowStrideBytes() const;

    // Сирий вказівник без типізації (потрібен явний каст під конкретну глибину)
    void *buffer() const;

    // Типізовані геттери з перевіркою поточної глибини кольору.
    // Повертають nullptr, якщо зображення завантажено з іншою глибиною.
    const uint16_t *bufferRGB565() const;
    const uint8_t *bufferRGB332() const;
    const uint8_t *bufferMono1() const; // формат, сумісний з Adafruit_GFX::drawBitmap

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

    // Яскравість пікселя RGB565 (0-255), зважена сума каналів (ITU-R BT.601)
    static inline uint8_t rgb565toGray(uint16_t c)
    {
        uint16_t r8 = (uint16_t)(((c >> 11) & 0x1F) * 255) / 31;
        uint16_t g8 = (uint16_t)(((c >> 5) & 0x3F) * 255) / 63;
        uint16_t b8 = (uint16_t)((c & 0x1F) * 255) / 31;
        return (uint8_t)((r8 * 299 + g8 * 587 + b8 * 114) / 1000);
    }

    // Встановлює/скидає один біт у рядку MONO1-буфера (MSB = лівий піксель)
    static inline void setMonoBit(uint8_t *rowBuffer, uint16_t x, bool white)
    {
        uint8_t mask = 0x80 >> (x & 7);
        if (white) {
            rowBuffer[x / 8] |= mask;
        } else {
            rowBuffer[x / 8] &= ~mask;
        }
    }

    void *_buffer; // uint16_t* для RGB565, uint8_t* для RGB332/MONO1
    uint16_t _width;
    uint16_t _height;
    JpegColorDepth _depth;
    bool _loaded;
    bool _usedPsram;
    uint8_t _monoThreshold = 128;

    const TLogger _logger{"jpeg"};

    static JpegImage *_activeInstance;
};