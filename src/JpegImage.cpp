#include "JpegImage.h"

#include <LittleFS.h>
#include <TJpg_Decoder.h>
#include <esp_heap_caps.h>

JpegImage *JpegImage::_activeInstance = nullptr;

JpegImage::JpegImage()
    : _buffer(nullptr), _width(0), _height(0), _loaded(false), _usedPsram(false)
{
}

JpegImage::~JpegImage()
{
    freeBuffer();
}

void JpegImage::freeBuffer()
{
    if (_buffer != nullptr) {
        heap_caps_free(_buffer);
        _buffer = nullptr;
    }
    _loaded = false;
    _width = 0;
    _height = 0;
}

// Допоміжна функція: спочатку намагаємось виділити пам'ять у PSRAM,
// якщо не вдалось (немає PSRAM або немає місця) - падаємо на звичайну RAM
static void *allocPreferPsram(size_t bytes, bool *usedPsram) {
    void *ptr = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (ptr != nullptr) {
        *usedPsram = true;
        return ptr;
    }

    //Serial.println("[JpegImage] PSRAM недоступна або немає місця, використовую внутрішню RAM");
    *usedPsram = false;
    return heap_caps_malloc(bytes, MALLOC_CAP_8BIT);
}

bool JpegImage::loadFromLittleFS(const char *path) {
    freeBuffer();

    if (!LittleFS.exists(path))
    {
        Serial.printf("[JpegImage] Файл не знайдено: %s\n", path);
        return false;
    }

    File file = LittleFS.open(path, "r");
    if (!file) {
        Serial.printf("[JpegImage] Не вдалось відкрити файл: %s\n", path);
        return false;
    }

    size_t fileSize = file.size();
    bool jpegBufPsram = false;
    uint8_t *jpegData = (uint8_t *)allocPreferPsram(fileSize, &jpegBufPsram);

    if (jpegData == nullptr) {
        Serial.println("[JpegImage] Недостатньо пам'яті для читання jpg-файлу");
        file.close();
        return false;
    }

    size_t bytesRead = file.read(jpegData, fileSize);
    file.close();

    if (bytesRead != fileSize) {
        Serial.println("[JpegImage] Розмір прочитаних даних не збігається з розміром файлу");
        heap_caps_free(jpegData);
        return false;
    }

    uint16_t jpegWidth = 0;
    uint16_t jpegHeight = 0;
    if (TJpgDec.getJpgSize(&jpegWidth, &jpegHeight, jpegData, fileSize) != JDR_OK) {
        Serial.println("[JpegImage] Не вдалось розпарсити заголовок jpg");
        heap_caps_free(jpegData);
        return false;
    }

    size_t bufferBytes = (size_t)jpegWidth * jpegHeight * sizeof(uint16_t);
    _buffer = (uint16_t *)allocPreferPsram(bufferBytes, &_usedPsram);
    if (_buffer == nullptr) {
        Serial.println("[JpegImage] Недостатньо пам'яті для декодованого зображення");
        heap_caps_free(jpegData);
        return false;
    }

    _width = jpegWidth;
    _height = jpegHeight;

    TJpgDec.setJpgScale(1);
    TJpgDec.setSwapBytes(true); // для прямої сумісності з TFT_eSPI/ST7789
    TJpgDec.setCallback(JpegImage::jpegOutputCallback);

    _activeInstance = this;
    JRESULT decodeResult = TJpgDec.drawJpg(0, 0, jpegData, fileSize);
    _activeInstance = nullptr;

    heap_caps_free(jpegData);

    if (decodeResult != JDR_OK) {
        Serial.println("[JpegImage] Помилка декодування jpg");
        freeBuffer();
        return false;
    }

    _loaded = true;
    Serial.printf("[JpegImage] Завантажено %dx%d, пам'ять: %s\n", _width, _height, _usedPsram ? "PSRAM" : "внутрішня RAM");

    return true;
}

bool JpegImage::jpegOutputCallback(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t *bitmap) {
    if (_activeInstance == nullptr || _activeInstance->_buffer == nullptr) {
        return false;
    }

    if (y >= _activeInstance->_height) {
        return true;
    }

    for (uint16_t row = 0; row < h; row++) {
        uint16_t destY = y + row;
        if (destY >= _activeInstance->_height) {
            break;
        }

        uint16_t copyWidth = w;
        if (x + copyWidth > _activeInstance->_width) {
            copyWidth = _activeInstance->_width - x;
        }

        uint16_t *destRow = _activeInstance->_buffer + (destY * _activeInstance->_width) + x;
        uint16_t *srcRow = bitmap + (row * w);
        memcpy(destRow, srcRow, copyWidth * sizeof(uint16_t));
    }

    return true;
}

bool JpegImage::isLoaded() const { return _loaded; }
uint16_t JpegImage::width() const { return _width; }
uint16_t JpegImage::height() const { return _height; }
uint16_t *JpegImage::buffer() const { return _buffer; }

/* void JpegImage::pushTo(TFT_eSprite &sprite, int32_t x, int32_t y) const
{
    if (!_loaded || _buffer == nullptr)
    {
        return;
    }
    sprite.pushImage(x, y, _width, _height, _buffer);
} */