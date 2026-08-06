// Setup_ST7789_lcd147.h
// User_Setup для bodmer/TFT_eSPI@^2.5.43
// Плата: Waveshare ESP32-S3-LCD-1.47 (ESP32-S3, ST7789, SPI, 172x320, PSRAM, SD)
// https://www.waveshare.com/wiki/ESP32-S3-LCD-1.47
//
// Підключається через build_flags у platformio.ini:
//   -include include/Setup_ST7789_lcd147.h
//
// Це ЄДИНЕ джерело істини для TFT_eSPI-макросів цієї плати (USER_SETUP_LOADED,
// ST7789_DRIVER, TFT_WIDTH/HEIGHT, піни, SPI_FREQUENCY, LOAD_FONT*) —
// у [env:esp32-s3-lcd147] platformio.ini ці макроси навмисно НЕ дублюються
// через -D, щоб уникнути редефініції.
//
// Джерело пінів — офіційний приклад Waveshare/Espressif (ws-s3-lcd-1-47):
// MOSI=45, SCLK=40, CS=42, DC=41, RST=39, Backlight=48.

#define USER_SETUP_LOADED 1

// ---------- Драйвер ----------
#define ST7789_DRIVER 1

// ---------- Розміри панелі ----------
#define TFT_WIDTH 172
#define TFT_HEIGHT 320

// ---------- Порядок кольорів / інверсія ----------
#define TFT_RGB_ORDER TFT_BGR
// Якщо картинка виглядає як "негатив" — розкоментуйте:
// #define TFT_INVERSION_ON

// ---------- Піни SPI (ESP32-S3, довільні GPIO) ----------
// Дисплей 1.47" не має MISO (тільки TX-напрямок) — явно позначаємо -1,
// інакше TFT_eSPI на ESP32-S3 може некоректно сконфігурувати SPI bus.
#define TFT_MISO -1
#define TFT_MOSI 45
#define TFT_SCLK 40
#define TFT_CS 42
#define TFT_DC 41
#define TFT_RST 39
#define TFT_BL 48
#define TFT_BACKLIGHT_ON HIGH

// ---------- SPI порт (ОБОВ'ЯЗКОВО для ESP32-S3!) ----------
// На ESP32-S3 макрос FSPI в Arduino core резолвиться в 0 (не є реальним
// номером SPI-порту), через що TFT_eSPI обчислює NULL-адресу регістра
// (REG_SPI_BASE(0) == NULL) і падає з StoreProhibited в SET_BUS_WRITE_MODE
// (begin_tft_write) при першому writecommand(). USE_HSPI_PORT примушує
// бібліотеку використовувати SPI_PORT=3 (реальний апаратний SPI3).
// Джерело: https://github.com/Bodmer/TFT_eSPI/discussions/3283
#define USE_HSPI_PORT

// ---------- SPI частота ----------
#define SPI_FREQUENCY 40000000
// #define SPI_READ_FREQUENCY  20000000

// ---------- Шрифти ----------
#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define LOAD_FONT6
#define LOAD_FONT7
#define LOAD_FONT8
