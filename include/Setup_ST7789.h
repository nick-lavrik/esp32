// Setup_ST7789.h
// User_Setup для bodmer/TFT_eSPI@^2.5.43
// Плата: ESP32-S3-DevKitC-1, дисплей: ST7789 (SPI, 240x320)
//
// Підключається через build_flags у platformio.ini:
//   -DUSER_SETUP_LOADED=1
//   -include include/Setup_ST7789.h
//
// УВАГА: номери пінів нижче — типові для проводки "своїми руками" на
// ESP32-S3-DevKitC-1. Звірте з вашою фактичною розводкою і скоригуйте.

#define USER_SETUP_LOADED 1
// #define USER_SETUP_ID 9001

// ---------- Драйвер ----------
#define ST7789_DRIVER 1     // Повна конфігурація (не ST7789_2_DRIVER)

// ---------- Розміри панелі ----------
#define TFT_WIDTH  240
#define TFT_HEIGHT 320

// ---------- Порядок кольорів / інверсія ----------
// Якщо колір "перевернутий" (напр. небо жовте замість синього) —
// перемкніть TFT_RGB_ORDER між TFT_RGB і TFT_BGR.
#define TFT_RGB_ORDER TFT_BGR
// Якщо картинка виглядає як "негатив" — розкоментуйте:
// #define TFT_INVERSION_ON

// ---------- Піни SPI (VSPI/HSPI на ESP32-S3, довільні GPIO) ----------
#define TFT_MISO 12 // new define
#define TFT_MOSI 13 // 11
#define TFT_SCLK 14 // 12
#define TFT_CS   15 // 10  // Chip select
#define TFT_DC    2 // 9   // Data/Command
#define TFT_RST  -1 // 8   // Reset (можна підключити до EN плати, тоді -1)
// #define TFT_BL   21 // 14  // Підсвітка (backlight)
// #define TFT_BACKLIGHT_ON HIGH

// ---------- SPI частота ----------
//#define SPI_FREQUENCY       40000000
//#define SPI_READ_FREQUENCY  20000000
//#define SPI_TOUCH_FREQUENCY  2500000

// ---------- Шрифти (щоб зменшити розмір прошивки — вимкніть зайве) ----------
// #define LOAD_GLCD
// #define LOAD_FONT2
// #define LOAD_FONT4
// #define LOAD_FONT6
// #define LOAD_FONT7
// #define LOAD_FONT8
//#define LOAD_GFXFF
//#define SMOOTH_FONT
