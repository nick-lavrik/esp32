#include "JpegImage.hpp"

#include <LittleFS.h>
#include <TJpg_Decoder.h>
#include <esp_heap_caps.h>

JpegImage *JpegImage::_activeInstance = nullptr;

JpegImage::JpegImage()
    : _buffer(nullptr), _width(0), _height(0),
      _depth(JpegColorDepth::RGB565), _loaded(false), _usedPsram(false)
{
}

JpegImage::~JpegImage()
{
    freeBuffer();
}

void JpegImage::freeBuffer()
{
    if (_buffer != nullptr)
    {
        heap_caps_free(_buffer);
        _buffer = nullptr;
    }
    _loaded = false;
    _width = 0;
    _height = 0;
}

// Спочатку намагаємось у PSRAM (якщо є), інакше - у звичайну RAM
static void *allocPreferPsram(size_t bytes, bool *usedPsram)
{
    void *ptr = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (ptr != nullptr)
    {
        *usedPsram = true;
        return ptr;
    }
    *usedPsram = false;
    return heap_caps_malloc(bytes, MALLOC_CAP_8BIT);
}

bool JpegImage::loadFromLittleFS(const char *path, JpegColorDepth depth)
{
    freeBuffer();
    _depth = depth;

    if (!LittleFS.exists(path))
    {
        Serial.printf("[JpegImage] Файл не знайдено: %s\n", path);
        return false;
    }

    File file = LittleFS.open(path, "r");
    if (!file)
    {
        Serial.printf("[JpegImage] Не вдалось відкрити файл: %s\n", path);
        return false;
    }

    size_t fileSize = file.size();
    bool jpegBufPsram = false;
    uint8_t *jpegData = (uint8_t *)allocPreferPsram(fileSize, &jpegBufPsram);
    if (jpegData == nullptr)
    {
        Serial.println("[JpegImage] Недостатньо пам'яті для читання jpg-файлу");
        file.close();
        return false;
    }

    size_t bytesRead = file.read(jpegData, fileSize);
    file.close();

    if (bytesRead != fileSize)
    {
        Serial.println("[JpegImage] Розмір прочитаних даних не збігається з розміром файлу");
        heap_caps_free(jpegData);
        return false;
    }

    uint16_t jpegWidth = 0;
    uint16_t jpegHeight = 0;
    if (TJpgDec.getJpgSize(&jpegWidth, &jpegHeight, jpegData, fileSize) != JDR_OK)
    {
        Serial.println("[JpegImage] Не вдалось розпарсити заголовок jpg");
        heap_caps_free(jpegData);
        return false;
    }

    size_t bytesPerPixel = (depth == JpegColorDepth::RGB565) ? 2 : 1;
    size_t bufferBytes = (size_t)jpegWidth * jpegHeight * bytesPerPixel;

    Serial.printf("[JpegImage] \"%s\" %dx%d (%d bytes) - free (%d)\n", path, jpegWidth, jpegHeight, bufferBytes, heap_caps_get_free_size(MALLOC_CAP_8BIT));

    _buffer = allocPreferPsram(bufferBytes, &_usedPsram);
    if (_buffer == nullptr)
    {
        Serial.printf("[JpegImage] Недостатньо пам'яті для декодованого зображення (%d / %d)\n", bufferBytes, heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) );
        heap_caps_free(jpegData);
        return false;
    }

    _width = jpegWidth;
    _height = jpegHeight;

    TJpgDec.setJpgScale(1);
    // RGB565 -> потрібен swap байтів для коректного порядку на ST7789
    // RGB332 -> конвертуємо самі з "чистого" RGB565, swap не потрібен
    // TJpgDec.setSwapBytes(depth == JpegColorDepth::RGB565); // ми вже робимо swap в Display.cpp
    TJpgDec.setCallback(JpegImage::jpegOutputCallback);

    _activeInstance = this;
    JRESULT decodeResult = TJpgDec.drawJpg(0, 0, jpegData, fileSize);
    _activeInstance = nullptr;

    heap_caps_free(jpegData);

    if (decodeResult != JDR_OK)
    {
        Serial.println("[JpegImage] Помилка декодування jpg");
        freeBuffer();
        return false;
    }

    _loaded = true;
    Serial.printf("[JpegImage] Завантажено %dx%d, глибина: %d біт, розмір буфера: %u байт, пам'ять: %s\n",
                  _width, _height, (int)depth, (unsigned)bufferBytes,
                  _usedPsram ? "PSRAM" : "внутрішня RAM");
    return true;
}

bool JpegImage::jpegOutputCallback(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t *bitmap)
{
    JpegImage *self = _activeInstance;
    if (self == nullptr || self->_buffer == nullptr)
    {
        return false;
    }

    if (y >= self->_height)
    {
        return true;
    }

    for (uint16_t row = 0; row < h; row++)
    {
        uint16_t destY = y + row;
        if (destY >= self->_height)
        {
            break;
        }

        uint16_t copyWidth = w;
        if (x + copyWidth > self->_width)
        {
            copyWidth = self->_width - x;
        }

        uint16_t *srcRow = bitmap + (row * w);

        if (self->_depth == JpegColorDepth::RGB565)
        {
            uint16_t *destRow = (uint16_t *)self->_buffer + (destY * self->_width) + x;
            memcpy(destRow, srcRow, copyWidth * sizeof(uint16_t));
        }
        else // RGB332 - конвертуємо піксель за пікселем
        {
            uint8_t *destRow = (uint8_t *)self->_buffer + (destY * self->_width) + x;
            for (uint16_t col = 0; col < copyWidth; col++)
            {
                destRow[col] = rgb565to332(srcRow[col]);
            }
        }
    }

    return true;
}

bool JpegImage::isLoaded() const { return _loaded; }
uint16_t JpegImage::width() const { return _width; }
uint16_t JpegImage::height() const { return _height; }
JpegColorDepth JpegImage::colorDepth() const { return _depth; }

size_t JpegImage::bufferSizeBytes() const
{
    size_t bpp = (_depth == JpegColorDepth::RGB565) ? 2 : 1;
    return (size_t)_width * _height * bpp;
}

void *JpegImage::buffer() const { return _buffer; }

const uint16_t *JpegImage::bufferRGB565() const
{
    if (_depth != JpegColorDepth::RGB565)
    {
        return nullptr;
    }
    return static_cast<const uint16_t *>(_buffer);
}

const uint8_t *JpegImage::bufferRGB332() const
{
    if (_depth != JpegColorDepth::RGB332)
    {
        return nullptr;
    }
    return static_cast<const uint8_t *>(_buffer);
}