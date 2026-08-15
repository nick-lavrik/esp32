#include "JpegImage.hpp"

#include <new>

#include <LittleFS.h>

// RISC-V (ESP32-C6) не толерантний до unaligned memory access, на відміну від
// Xtensa (ESP32/ESP32-S3). tjpgd (движок під TJpg_Decoder) в парсингу заголовка
// читає multi-byte поля без гарантії вирівнювання - на C6 це давало биті/нульові
// значення (спостерігалось: getJpgSize() повертав width>0, height=0). Тому на C6
// декодуємо через JPEGDEC (bitbank2), яка коректно працює на RISC-V.
#if defined(BOARD_ESP32_C6)
#include <JPEGDEC.h>
#else
#include <TJpg_Decoder.h>
#endif

#if defined(ESP32)
#include <esp_heap_caps.h>
#else
// ESP8266 core не має esp_heap_caps.h (це ESP-IDF API) і не має PSRAM -
// підміняємо тими самими іменами, що зводяться до звичайних malloc/free.
static inline void *heap_caps_malloc(size_t size, uint32_t) { return malloc(size); }
static inline void heap_caps_free(void *ptr) { free(ptr); }
static inline size_t heap_caps_get_free_size(uint32_t) { return ESP.getFreeHeap(); }
static inline size_t heap_caps_get_largest_free_block(uint32_t) { return ESP.getFreeHeap(); }
#define MALLOC_CAP_SPIRAM 0
#define MALLOC_CAP_8BIT 0
#endif

JpegImage *JpegImage::_activeInstance = nullptr;

JpegImage::JpegImage()
    : _buffer(nullptr),
      _width(0),
      _height(0),
      _depth(JpegColorDepth::RGB565),
      _loaded(false),
      _usedPsram(false) {}

JpegImage::~JpegImage() { freeBuffer(); }

void JpegImage::freeBuffer() {
  if (_buffer != nullptr) {
    heap_caps_free(_buffer);
    _buffer = nullptr;
  }
  _loaded = false;
  _width = 0;
  _height = 0;
}

// Спочатку намагаємось у PSRAM (якщо є), інакше - у звичайну RAM
static void *allocPreferPsram(size_t bytes, bool *usedPsram) {
  void *ptr = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (ptr != nullptr) {
    *usedPsram = true;
    return ptr;
  }
  *usedPsram = false;
  return heap_caps_malloc(bytes, MALLOC_CAP_8BIT);
}

bool JpegImage::loadFromLittleFS(const char *path, JpegColorDepth depth) {
  freeBuffer();
  _depth = depth;

  if (!LittleFS.exists(path)) {
    _logger.error("File not found: %s", path);
    return false;
  }

  File file = LittleFS.open(path, "rb");
  if (!file) {
    _logger.error("Can't open file: %s", path);
    return false;
  }

  file.seek(0);

  size_t fileSize = file.size();

  if (fileSize <= 0) {
    _logger.error("Empty jpeg file \"%s\" = size=%d", path, fileSize);
    return false;
  }

  bool jpegBufPsram = false;
  uint8_t *jpegData = (uint8_t *)allocPreferPsram(fileSize, &jpegBufPsram);
  if (jpegData == nullptr) {
    _logger.error("Not enough memory to read jpg file");
    file.close();
    return false;
  }

  size_t bytesRead = file.read(jpegData, fileSize);
  file.close();

  if (bytesRead != fileSize) {
    _logger.error("The size of the read data does not match the file size (%d <=> %d)", bytesRead, fileSize);
    heap_caps_free(jpegData);
    return false;
  }

  uint16_t jpegWidth = 0;
  uint16_t jpegHeight = 0;

#if defined(BOARD_ESP32_C6)
  // JPEGDEC::_jpeg (JPEGIMAGE) - це ~17.5 KB вбудованої структури (буфери
  // Huffman-таблиць, MCU, пікселів), НЕ вказівник. Локальна змінна на стеку
  // переповнює loopTask stack (типово 8 KB) -> "Stack protection fault".
  // Тому виділяємо JPEGDEC на heap (за можливості - в PSRAM).
  bool jpegDecoderPsram = false;
  JPEGDEC *jpegDecoder = (JPEGDEC *)allocPreferPsram(sizeof(JPEGDEC), &jpegDecoderPsram);
  if (jpegDecoder == nullptr) {
    _logger.error("Not enough memory for JPEGDEC instance (%u bytes)", (unsigned)sizeof(JPEGDEC));
    heap_caps_free(jpegData);
    return false;
  }
  new (jpegDecoder) JPEGDEC();

  if (!jpegDecoder->openRAM(jpegData, (int)fileSize, JpegImage::jpegDrawCallback)) {
    _logger.error("Can't parse jpeg header (JPEGDEC error %d)", jpegDecoder->getLastError());
    jpegDecoder->~JPEGDEC();
    heap_caps_free(jpegDecoder);
    heap_caps_free(jpegData);
    return false;
  }
  jpegWidth = (uint16_t)jpegDecoder->getWidth();
  jpegHeight = (uint16_t)jpegDecoder->getHeight();
#else
  if (TJpgDec.getJpgSize(&jpegWidth, &jpegHeight, jpegData, fileSize) != JDR_OK) {
    _logger.error("Can't perse jpeg header");
    heap_caps_free(jpegData);
    return false;
  }
#endif

  size_t bufferBytes;
  if (depth == JpegColorDepth::MONO1) {
    bufferBytes = (size_t)((jpegWidth + 7) / 8) * jpegHeight;
  } else {
    size_t bytesPerPixel = 1;
    if (depth == JpegColorDepth::RGB888) {
      bytesPerPixel = 3;
    } else if (depth == JpegColorDepth::RGB565) {
      bytesPerPixel = 2;
    }
    bufferBytes = (size_t)jpegWidth * jpegHeight * bytesPerPixel;
  }

  _logger.info("\"%s\" %dx%d (%d:%d bytes) - free (%d)", path, jpegWidth, jpegHeight, bufferBytes, fileSize,
               heap_caps_get_free_size(MALLOC_CAP_8BIT));

  _buffer = allocPreferPsram(bufferBytes, &_usedPsram);
  if (_buffer == nullptr) {
    _logger.error("Недостатньо пам'яті для декодованого зображення (%d / %d)", bufferBytes,
                  heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
#if defined(BOARD_ESP32_C6)
    jpegDecoder->~JPEGDEC();
    heap_caps_free(jpegDecoder);
#endif
    heap_caps_free(jpegData);
    return false;
  }

  _width = jpegWidth;
  _height = jpegHeight;

  _activeInstance = this;

#if defined(BOARD_ESP32_C6)
  jpegDecoder->setPixelType(RGB565_LITTLE_ENDIAN);
  int decodeResult = jpegDecoder->decode(0, 0, 0);
  int jpegDecError = jpegDecoder->getLastError();
  jpegDecoder->close();
  jpegDecoder->~JPEGDEC();
  heap_caps_free(jpegDecoder);
  _activeInstance = nullptr;

  heap_caps_free(jpegData);

  if (decodeResult != 1) {
    _logger.error("Jpeg decode error \"%s\" (JPEGDEC error %d)", path, jpegDecError);
    freeBuffer();
    return false;
  }
#else
  TJpgDec.setJpgScale(1);
  // RGB565 -> потрібен swap байтів для коректного порядку на ST7789
  // RGB332 -> конвертуємо самі з "чистого" RGB565, swap не потрібен
  // TJpgDec.setSwapBytes(depth == JpegColorDepth::RGB565); // ми вже робимо swap в Display.cpp
  TJpgDec.setCallback(JpegImage::jpegOutputCallback);

  JRESULT decodeResult = TJpgDec.drawJpg(0, 0, jpegData, fileSize);
  _activeInstance = nullptr;

  heap_caps_free(jpegData);

  if (decodeResult != JDR_OK) {
    _logger.error("Jpeg decode error \"%s\"", path);
    freeBuffer();
    return false;
  }
#endif

  _loaded = true;
  _logger.info("Loaded %dx%d, depth: %d біт, buffer: %u B, memory: %s", _width,
               _height, (int)depth, (unsigned)bufferBytes, _usedPsram ? "PSRAM" : "RAM");
  return true;
}

// Спільна логіка запису одного прямокутного блоку RGB565-пікселів (MCU-блок від
// JPEGDEC або рядковий блок від TJpg_Decoder) у вихідний буфер потрібної глибини.
// stride - крок між рядками вхідного bitmap у пікселях (для TJpg_Decoder == w,
// для JPEGDEC MCU-блоку теж == iWidth, тому параметр спільний для обох).
void JpegImage::blitBlock(int x, int y, int w, int h, int stride, const uint16_t *bitmap) {
  if (_buffer == nullptr || y >= _height) {
    return;
  }

  for (int row = 0; row < h; row++) {
    int destY = y + row;
    if (destY >= _height) {
      break;
    }

    int copyWidth = w;
    if (x + copyWidth > _width) {
      copyWidth = _width - x;
    }
    if (copyWidth <= 0) {
      continue;
    }

    const uint16_t *srcRow = bitmap + (size_t)row * stride;

    if (_depth == JpegColorDepth::RGB888) {
      uint8_t *destRow = (uint8_t *)_buffer + (size_t)(destY * _width + x) * 3;
      for (int col = 0; col < copyWidth; col++) {
        rgb565to888(srcRow[col], destRow + (size_t)col * 3);
      }
    } else if (_depth == JpegColorDepth::RGB565) {
      uint16_t *destRow = (uint16_t *)_buffer + (destY * _width) + x;
      memcpy(destRow, srcRow, copyWidth * sizeof(uint16_t));
    } else if (_depth == JpegColorDepth::RGB332) {
      // RGB332 - конвертуємо піксель за пікселем
      uint8_t *destRow = (uint8_t *)_buffer + (destY * _width) + x;
      for (int col = 0; col < copyWidth; col++) {
        destRow[col] = rgb565to332(srcRow[col]);
      }
    } else {
      // MONO1 - поріг яскравості, пакування в біти (формат Adafruit_GFX::drawBitmap)
      uint8_t *destRow = (uint8_t *)_buffer + (destY * rowStrideBytes());
      for (int col = 0; col < copyWidth; col++) {
        int destX = x + col;
        bool white = rgb565toGray(srcRow[col]) >= _monoThreshold;
        setMonoBit(destRow, destX, white);
      }
    }
  }
}

#if defined(BOARD_ESP32_C6)
int JpegImage::jpegDrawCallback(JPEGDRAW *pDraw) {
  JpegImage *self = _activeInstance;
  if (self == nullptr) {
    return 0;
  }
  self->blitBlock(pDraw->x, pDraw->y, pDraw->iWidth, pDraw->iHeight, pDraw->iWidth, pDraw->pPixels);
  return 1;
}
#else
bool JpegImage::jpegOutputCallback(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t *bitmap) {
  JpegImage *self = _activeInstance;
  if (self == nullptr || self->_buffer == nullptr) {
    return false;
  }
  self->blitBlock(x, y, w, h, w, bitmap);
  return true;
}
#endif

bool JpegImage::isLoaded() const { return _loaded; }
uint16_t JpegImage::width() const { return _width; }
uint16_t JpegImage::height() const { return _height; }
JpegColorDepth JpegImage::colorDepth() const { return _depth; }

size_t JpegImage::bufferSizeBytes() const {
  if (_depth == JpegColorDepth::MONO1) {
    return rowStrideBytes() * _height;
  }
  size_t bpp = 1;
  if (_depth == JpegColorDepth::RGB888) {
    bpp = 3;
  } else if (_depth == JpegColorDepth::RGB565) {
    bpp = 2;
  }
  return (size_t)_width * _height * bpp;
}

size_t JpegImage::rowStrideBytes() const {
  if (_depth != JpegColorDepth::MONO1) {
    return 0;
  }
  return (size_t)(_width + 7) / 8;
}

void JpegImage::setMonoThreshold(uint8_t threshold) { _monoThreshold = threshold; }

void *JpegImage::buffer() const { return _buffer; }

const uint8_t *JpegImage::bufferRGB888() const {
  if (_depth != JpegColorDepth::RGB888) {
    return nullptr;
  }

  return static_cast<const uint8_t *>(_buffer);
}

const uint16_t *JpegImage::bufferRGB565() const {
  if (_depth != JpegColorDepth::RGB565) {
    return nullptr;
  }

  return static_cast<const uint16_t *>(_buffer);
}

const uint8_t *JpegImage::bufferRGB332() const {
  if (_depth != JpegColorDepth::RGB332) {
    return nullptr;
  }

  return static_cast<const uint8_t *>(_buffer);
}

const uint8_t *JpegImage::bufferMono1() const {
  if (_depth != JpegColorDepth::MONO1) {
    return nullptr;
  }

  return static_cast<const uint8_t *>(_buffer);
}