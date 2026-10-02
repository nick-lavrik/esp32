#include "Background.hpp"

#include <Arduino.h>
#include <math.h>

#include <ImageEffects.hpp>
#include <Journal.hpp>
#include <Logger.hpp>
#include <SerialSink.hpp>
#include <TLogger.hpp>

#include "App/AppGlobals.hpp"
#include "BackgroundImages.hpp"

#if defined(ESP32)
#include <esp_log.h>
#endif

void setupBackgroundImage() {
#if defined(BACKGROUND_PROGMEM_HEADER)
  // Фон запечений у Flash (див. BackgroundImages.cpp) - декодований буфер у RAM
  // не потрібен зовсім. Саме тут і економляться 110 КБ на 320x172 RGB565.
  //
  // Ціна: ефекти (blur/tint/desaturate) до такого фону не застосувати - він
  // read-only. Тому запікати треба ВЖЕ з ефектами: зібрати з
  // LITTLEFS_BACKGROUND_IMAGE і SPRITE_COLOR_DEPTH=16, підібрати вигляд
  // командами, потім 'bg-dump' (або ./esp bg-save assets/<file>.h).
  Logger::info("background: baked into flash, RAM buffer skipped");
#elif defined(LITTLEFS_BACKGROUND_IMAGE)
  spaceImage.loadFromLittleFS(LITTLEFS_BACKGROUND_IMAGE,
                              SPRITE_COLOR_DEPTH > 8 ? JpegColorDepth::RGB565 : JpegColorDepth::RGB332);  // 16 | 8
  setBackgroundImage(spaceImage);
#if BOARD_4848S040
  ImageEffects::applyDesaturate(spaceImage, 0.3);
  ImageEffects::applyDarken(spaceImage, 0.25);
#endif

#if BOARD_ESP32_C6 || defined(BOARD_ESP32_C6_LCD096)
  ImageEffects::applyDesaturate(spaceImage, 0.3);
  // ImageEffects::applyBoxBlur(spaceImage, 2);
  ImageEffects::applyDarken(spaceImage, 0.20);
#endif

#if BOARD_ESP32_S3_LCD147
  ImageEffects::applyDesaturate(spaceImage, 0.3);
  // ImageEffects::applyBoxBlur(spaceImage, 2);
  ImageEffects::applyVignette(spaceImage, 0.3);
  ImageEffects::applyDarken(spaceImage, 0.3);
#endif
#endif
}

#if defined(LITTLEFS_BACKGROUND_IMAGE)

namespace {

// printf для машинних дампів: форматує в буфер і віддає в SerialSink::writeRaw()
// - повз журнал, але під тим самим замком, що й приймач. Єдиний користувач -
// 'bg-dump' (див. коментар у SerialSink.hpp, чому виняток саме тут).
uint32_t rawLost = 0;   // шматків, які не вийшли цілими
uint32_t rawBytes = 0;  // скільки байтів віддано в Serial (звіряти з отриманим)

void rawPrintf(const char* fmt, ...) {
  char buf[160];
  va_list args;
  va_start(args, fmt);
  const int written = vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
  if (written <= 0) return;
  const size_t length = (size_t)written < sizeof(buf) ? (size_t)written : sizeof(buf) - 1;
  if (!SerialSink::writeRaw(buf, length))
    ++rawLost;
  else
    rawBytes += length;
}

}  // namespace

void registerBackgroundCommands(SerialCommander& commander) {
  // Дамп ПОТОЧНОГО фону (тобто вже з накладеними ефектами: blur/desaturate/
  // darken із setupBackgroundImage() і команд) у вигляді C-хедера. Сенс саме в
  // ефектах: data/convert.c робить те саме з ОРИГІНАЛЬНОГО jpeg, тобто без них.
  //
  // Готовий хедер підключається через -D BACKGROUND_PROGMEM_HEADER (див.
  // platformio.ini) і тоді фон живе у Flash, а не в RAM - на 320x172 це
  // 110 КБ RAM, найбільший одиничний споживач на цій платі.
  //
  // Вивід іде напряму в Serial, без Logger: жодних префіксів, щоб файл можна
  // було зберегти байт-у-байт (див. ./esp bg-save).
  commander.registerCommand(
      "bg-dump", "dump current background (with effects) as C header for assets/", [](const String args) {
        static TLogger _log{"bg"};
        if (!spaceImage.isLoaded()) {
          _log.error("background is not loaded");
          return;
        }
        if (spaceImage.colorDepth() != JpegColorDepth::RGB565) {
          _log.error(
              "background is %d-bit, not RGB565 - rebuild with "
              "SPRITE_COLOR_DEPTH=16 to dump full quality",
              (int)spaceImage.colorDepth());
          return;
        }

        const uint16_t width = spaceImage.width();
        const uint16_t height = spaceImage.height();
        const uint16_t* pixels = spaceImage.bufferRGB565();
        if (pixels == nullptr) {
          _log.error("no RGB565 buffer");
          return;
        }

        // Глушимо логи на весь час дампу: інакше чужий рядок вклиниться ПОСЕРЕДИНІ
        // рядка з пікселями (MQTT сипле логи щосекунди, а дамп триває десятки
        // секунд) - і фільтр по префіксу в './esp bg-save' такого вже не врятує.
        const LogLevel savedLevel = Journal::instance().defaultLevel();
        Journal::instance().setDefaultLevel(LogLevel::Error);

        // І ОКРЕМО - лог самого ESP-IDF. Він пише в UART напряму, повз журнал і
        // повз замок SerialSink, тому наш фільтр рівня на нього не діє. Перевірено
        // на залізі: у дамп влізло "[E][ssl_client.cpp:127] ... Host is
        // unreachable" від ecoflow, який саме перепідключався.
        const esp_log_level_t savedIdfLevel = esp_log_level_get("*");
        esp_log_level_set("*", ESP_LOG_NONE);
        rawLost = 0;
        rawBytes = 0;

        // Сирий вивід повз журнал - інакше кожен рядок дістав би префікс
        // "[I][tag    ] " і заголовок не скомпілювався б. SerialSink::writeRaw()
        // пише під тим самим замком, що й приймач журналу, і чекає місця в TX
        // (див. коментар у SerialSink.hpp).
        rawPrintf("// generated by 'bg-dump' from %s (%ux%u, effects applied)\n", LITTLEFS_BACKGROUND_IMAGE,
                  (unsigned)width, (unsigned)height);
        rawPrintf("#pragma once\n");
        rawPrintf("#include <pgmspace.h>\n");
        rawPrintf("#define HAS_BACKGROUND_PROGMEM_RGB565 1\n");
        rawPrintf("#define BACKGROUND_PROGMEM_WIDTH  %u\n", (unsigned)width);
        rawPrintf("#define BACKGROUND_PROGMEM_HEIGHT %u\n", (unsigned)height);
        // Розмір масиву - width*height ЕЛЕМЕНТІВ по 2 байти (не width*height*2:
        // це вдвічі більше, ніж потрібно).
        rawPrintf("const uint16_t background_progmem_rgb565[%uu * %uu] PROGMEM = {\n", (unsigned)width,
                  (unsigned)height);

        // Пікселі йдуть пачками по 16 значень в один writeRaw(): 24 тисячі
        // окремих викликів коштували б 24 тисячі захоплень замка.
        const uint32_t total = (uint32_t)width * height;
        char row[16 * 7 + 2];
        size_t used = 0;
        for (uint32_t i = 0; i < total; i++) {
          used += snprintf(row + used, sizeof(row) - used, "0x%04X,", pixels[i]);
          if ((i + 1) % 16 == 0 || i + 1 == total) {
            used += snprintf(row + used, sizeof(row) - used, "\n");
            if (!SerialSink::writeRaw(row, used))
              ++rawLost;
            else
              rawBytes += used;
            used = 0;
          }
        }
        rawPrintf("};\n");

        // Serial.flush() ТУТ НЕ КЛИКАТИ. На USB CDC цієї плати він не дочікує
        // TX, а ВИКИДАЄ його: з flush() наприкінці дамп регулярно приїжджав без
        // "};" і без хвоста останнього рядка, а спроба флашити кожні 64 рядки
        // з'їдала 1700 значень із 12800. Без нього три прогони поспіль дали
        // рівно 90701 байт - стільки ж, скільки пристрій віддав у Serial.
        // Замість флашу - пауза: USB встигає вивезти буфер сам.
        delay(200);

        esp_log_level_set("*", savedIdfLevel);
        Journal::instance().setDefaultLevel(savedLevel);
        _log.debug("dump: %u bytes handed to serial", (unsigned)rawBytes);
        if (rawLost > 0) {
          _log.error("dump incomplete: %u chunk(s) did not fit into serial TX", (unsigned)rawLost);
        }
      });

  commander.registerCommand(
      "blur", "blur background image: blur <radius 1-8> [passes 1-3, default 1]", [](const String& args) {
        if (args.length() == 0) {
          Logger::info("use: blur <radius 1-8> [passes 1-3, default 1]");
          return;
        }

        int spaceIdx = args.indexOf(' ');
        int radius = (spaceIdx < 0 ? args : args.substring(0, spaceIdx)).toInt();
        int passes = (spaceIdx < 0) ? 1 : args.substring(spaceIdx + 1).toInt();
        if (passes < 1) passes = 1;

        if (radius < 1 || radius > 8) {
          Logger::info("radius must be 1-8");
          return;
        }
        if (passes > 3) {
          Logger::info("passes clamped to 3 (heavier passes take long on-device)");
          passes = 3;
        }

        bool ok = ImageEffects::applyBoxBlur(spaceImage, (uint8_t)radius, (uint8_t)passes);
        Logger::info(ok ? "blur applied: radius=%d passes=%d" : "blur failed (image not loaded?)", radius, passes);
      });

  commander.registerCommand(
      "tint", "tint background image: tint <RRGGBB hex> [alpha 0.0-1.0, default 0.5]", [](const String& args) {
        if (args.length() == 0) {
          Logger::info("use: tint <RRGGBB hex> [alpha 0.0-1.0, default 0.5]");
          return;
        }

        int spaceIdx = args.indexOf(' ');
        String hex = (spaceIdx < 0 ? args : args.substring(0, spaceIdx));
        float alpha = (spaceIdx < 0) ? 0.5f : args.substring(spaceIdx + 1).toFloat();

        if (hex.length() != 6) {
          Logger::info("color must be 6 hex chars, e.g. FF8800");
          return;
        }
        if (alpha < 0.0f) alpha = 0.0f;
        if (alpha > 1.0f) alpha = 1.0f;

        uint32_t rgb = strtoul(hex.c_str(), nullptr, 16);
        Pixel tint = Pixel::unpack(rgb);

        bool ok = ImageEffects::applyTint(spaceImage, tint, alpha);
        Logger::info(ok ? "tint applied: color=%s alpha=%.2f" : "tint failed (image not loaded?)", hex.c_str(), alpha);
      });

  commander.registerCommand("contrast", "contrast background image: contrast <factor 0.0-1.0>", [](const String& args) {
    if (args.length() == 0) {
      Logger::info("use: contrast <factor 0.0-1.0>");
      return;
    }

    float factor = args.toFloat();

    bool ok = ImageEffects::applyContrast(spaceImage, factor);
    Logger::info(ok ? "contrast applied: factor=%f" : "contrast failed (image not loaded?)", factor);
  });

  commander.registerCommand("sepia", "sepia background image: sepia <amount 0.0-1.0>", [](const String& args) {
    if (args.length() == 0) {
      Logger::info("use: sepia <amount 0.0-1.0>");
      return;
    }

    float amount = args.toFloat();

    bool ok = ImageEffects::applySepia(spaceImage, amount);
    Logger::info(ok ? "sepia applied: amount=%f" : "sepia failed (image not loaded?)", amount);
  });

  commander.registerCommand(
      "desaturate", "desaturate background image: desaturate <factor 0.0-1.0>", [](const String& args) {
        if (args.length() == 0) {
          Logger::info("use: desaturate <factor 0.0-1.0>");
          return;
        }

        float factor = args.toFloat();

        bool ok = ImageEffects::applyDesaturate(spaceImage, factor);
        Logger::info(ok ? "desaturate applied: factor=%f" : "desaturate failed (image not loaded?)", factor);
      });

  commander.registerCommand("darken", "darken background image: darken <factor 0.0-1.0>", [](const String& args) {
    if (args.length() == 0) {
      Logger::info("use: darken <factor 0.0-1.0>");
      return;
    }

    float factor = args.toFloat();

    bool ok = ImageEffects::applyDarken(spaceImage, factor);
    Logger::info(ok ? "darken applied: factor=%f" : "darken failed (image not loaded?)", factor);
  });

  commander.registerCommand("lighten", "lighten background image: lighten <factor 0.0-1.0>", [](const String& args) {
    if (args.length() == 0) {
      Logger::info("use: lighten <factor 0.0-1.0>");
      return;
    }

    float factor = args.toFloat();

    bool ok = ImageEffects::applyLighten(spaceImage, factor);
    Logger::info(ok ? "lighten applied: factor=%f" : "lighten failed (image not loaded?)", factor);
  });

  commander.registerCommand("invert", "invert background image colors, no args", [](const String& args) {
    bool ok = ImageEffects::applyInvert(spaceImage);
    Logger::info(ok ? "invert applied" : "invert failed (image not loaded?)");
  });

  commander.registerCommand(
      "threshold", "threshold background image: threshold <level 0.0-1.0, default 0.5>", [](const String& args) {
        float threshold = (args.length() == 0) ? 0.5f : args.toFloat();

        bool ok = ImageEffects::applyThreshold(spaceImage, threshold);
        Logger::info(ok ? "threshold applied: level=%f" : "threshold failed (image not loaded?)", threshold);
      });

  commander.registerCommand("hue", "rotate hue of background image: hue <angle degrees 0-360>", [](const String& args) {
    if (args.length() == 0) {
      Logger::info("use: hue <angle degrees 0-360>");
      return;
    }

    float degrees = args.toFloat();
    float radians = degrees * (float)M_PI / 180.0f;

    bool ok = ImageEffects::applyHueRotate(spaceImage, radians);
    Logger::info(ok ? "hue applied: degrees=%f" : "hue failed (image not loaded?)", degrees);
  });

  commander.registerCommand("thermal", "thermal-camera effect on background image, no args", [](const String& args) {
    bool ok = ImageEffects::applyThermal(spaceImage);
    Logger::info(ok ? "thermal applied" : "thermal failed (image not loaded?)");
  });

  commander.registerCommand("gamma", "gamma-correct background image: gamma <value, e.g. 1.4>", [](const String& args) {
    if (args.length() == 0) {
      Logger::info("use: gamma <value, e.g. 1.4>");
      return;
    }

    float gamma = args.toFloat();
    if (gamma <= 0.0f) {
      Logger::info("gamma must be > 0.0");
      return;
    }

    bool ok = ImageEffects::applyGamma(spaceImage, gamma);
    Logger::info(ok ? "gamma applied: value=%f" : "gamma failed (image not loaded?)", gamma);
  });

  commander.registerCommand("posterize", "posterize background image: posterize <levels, >=2>", [](const String& args) {
    if (args.length() == 0) {
      Logger::info("use: posterize <levels, >=2>");
      return;
    }

    int levels = args.toInt();
    if (levels < 2) {
      Logger::info("levels must be >= 2");
      return;
    }

    bool ok = ImageEffects::applyPosterize(spaceImage, levels);
    Logger::info(ok ? "posterize applied: levels=%d" : "posterize failed (image not loaded?)", levels);
  });

  commander.registerCommand(
      "solarize", "solarize background image: solarize <threshold 0.0-1.0, default 0.5>", [](const String& args) {
        float threshold = (args.length() == 0) ? 0.5f : args.toFloat();

        bool ok = ImageEffects::applySolarize(spaceImage, threshold);
        Logger::info(ok ? "solarize applied: threshold=%f" : "solarize failed (image not loaded?)", threshold);
      });

  commander.registerCommand(
      "duotone", "duotone background image: duotone <dark RRGGBB> <light RRGGBB>", [](const String& args) {
        int spaceIdx = args.indexOf(' ');
        if (spaceIdx < 0) {
          Logger::info("use: duotone <dark RRGGBB> <light RRGGBB>");
          return;
        }

        String darkHex = args.substring(0, spaceIdx);
        String lightHex = args.substring(spaceIdx + 1);
        lightHex.trim();

        if (darkHex.length() != 6 || lightHex.length() != 6) {
          Logger::info("both colors must be 6 hex chars, e.g. duotone 1a0033 ffcc88");
          return;
        }

        Pixel dark = Pixel::unpack((uint32_t)strtoul(darkHex.c_str(), nullptr, 16));
        Pixel light = Pixel::unpack((uint32_t)strtoul(lightHex.c_str(), nullptr, 16));

        bool ok = ImageEffects::applyDuotone(spaceImage, dark, light);
        Logger::info(ok ? "duotone applied: dark=%s light=%s" : "duotone failed (image not loaded?)", darkHex.c_str(),
                     lightHex.c_str());
      });

  commander.registerCommand(
      "balance", "color-balance background image: balance <rMul> <gMul> <bMul>", [](const String& args) {
        String rest = args;
        rest.trim();
        int i1 = rest.indexOf(' ');
        if (i1 < 0) {
          Logger::info("use: balance <rMul> <gMul> <bMul>");
          return;
        }
        String tok1 = rest.substring(0, i1);
        rest = rest.substring(i1 + 1);
        rest.trim();
        int i2 = rest.indexOf(' ');
        if (i2 < 0) {
          Logger::info("use: balance <rMul> <gMul> <bMul>");
          return;
        }
        String tok2 = rest.substring(0, i2);
        String tok3 = rest.substring(i2 + 1);

        float rMul = tok1.toFloat();
        float gMul = tok2.toFloat();
        float bMul = tok3.toFloat();

        bool ok = ImageEffects::applyColorBalance(spaceImage, rMul, gMul, bMul);
        Logger::info(ok ? "balance applied: r=%f g=%f b=%f" : "balance failed (image not loaded?)", rMul, gMul, bMul);
      });

  commander.registerCommand(
      "noise", "noise: add grain noise to background image: noise <amount 0.0-1.0>", [](const String& args) {
        if (args.length() == 0) {
          Logger::info("use: noise <amount 0.0-1.0>");
          return;
        }

        float amount = args.toFloat();

        bool ok = ImageEffects::applyNoise(spaceImage, amount);
        Logger::info(ok ? "noise applied: amount=%f" : "noise failed (image not loaded?)", amount);
      });

  commander.registerCommand(
      "vignette", "vignette background image: vignette <strength 0.0-1.0>", [](const String& args) {
        if (args.length() == 0) {
          Logger::info("use: vignette <strength 0.0-1.0>");
          return;
        }

        float strength = args.toFloat();

        bool ok = ImageEffects::applyVignette(spaceImage, strength);
        Logger::info(ok ? "vignette applied: strength=%f" : "vignette failed (image not loaded?)", strength);
      });

  commander.registerCommand("pixelate", "pixelate background image: pixelate <blockSize, >=2>", [](const String& args) {
    if (args.length() == 0) {
      Logger::info("use: pixelate <blockSize, >=2>");
      return;
    }

    int blockSize = args.toInt();
    if (blockSize < 2 || blockSize > 255) {
      Logger::info("blockSize must be 2-255");
      return;
    }

    bool ok = ImageEffects::applyPixelate(spaceImage, (uint8_t)blockSize);
    Logger::info(ok ? "pixelate applied: blockSize=%d" : "pixelate failed (image not loaded?)", blockSize);
  });

  commander.registerCommand(
      "scanlines", "scanlines on background image: scanlines <darkenFactor 0.0-1.0>", [](const String& args) {
        if (args.length() == 0) {
          Logger::info("use: scanlines <darkenFactor 0.0-1.0>");
          return;
        }

        float darkenFactor = args.toFloat();

        bool ok = ImageEffects::applyScanlines(spaceImage, darkenFactor);
        Logger::info(ok ? "scanlines applied: darkenFactor=%f" : "scanlines failed (image not loaded?)", darkenFactor);
      });

  commander.registerCommand(
      "chromatic", "chromatic aberration on background image: chromatic <offsetPx, >=1>", [](const String& args) {
        if (args.length() == 0) {
          Logger::info("use: chromatic <offsetPx, >=1>");
          return;
        }

        int offsetPx = args.toInt();
        if (offsetPx < 1 || offsetPx > 255) {
          Logger::info("offsetPx must be 1-255");
          return;
        }

        bool ok = ImageEffects::applyChromaticAberration(spaceImage, (uint8_t)offsetPx);
        Logger::info(ok ? "chromatic applied: offsetPx=%d" : "chromatic failed (image not loaded?)", offsetPx);
      });

  commander.registerCommand("sobel", "edge detection (Sobel) on background image, no args", [](const String& args) {
    bool ok = ImageEffects::applySobelEdges(spaceImage);
    Logger::info(ok ? "sobel applied" : "sobel failed (image not loaded, or smaller than 3x3?)");
  });

  commander.registerCommand(
      "emboss", "emboss background image: emboss [strength, default 1.0]", [](const String& args) {
        float strength = (args.length() == 0) ? 1.0f : args.toFloat();

        bool ok = ImageEffects::applyEmboss(spaceImage, strength);
        Logger::info(ok ? "emboss applied: strength=%f" : "emboss failed (image not loaded, or smaller than 3x3?)",
                     strength);
      });

  commander.registerCommand("dither", "ordered dithering (Bayer 8x8) on background image, no args",
                            [](const String& args) {
                              bool ok = false;
                              switch (spaceImage.colorDepth()) {
                                case JpegColorDepth::RGB332:
                                  ok = ImageEffects::applyDitheringRGB332(spaceImage);
                                  break;
                                case JpegColorDepth::RGB565:
                                  ok = ImageEffects::applyDitheringRGB565(spaceImage);
                                  break;
                                case JpegColorDepth::RGB888:
                                  ok = ImageEffects::applyDitheringRGB888(spaceImage);
                                  break;
                                default:
                                  Logger::info("dither: unsupported color depth (MONO1?)");
                                  return;
                              }
                              Logger::info(ok ? "dither applied" : "dither failed (image not loaded?)");
                            });

  commander.registerCommand("background", "load background image: background <LittleFS path>", [](const String& args) {
    if (args.length() == 0) {
      Logger::info("use: background <LittleFS path>");
      return;
    }

    spaceImage.loadFromLittleFS(args.c_str());
  });
}

#else

void registerBackgroundCommands(SerialCommander&) {}

#endif
