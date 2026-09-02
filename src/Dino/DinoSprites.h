#pragma once

#include <MonoBitmap.hpp>

// Спрайти гри Chrome Dino (1 біт/піксель, PROGMEM).
//
//   display.drawBitmap(x, y, DinoArt::trexRunA().data(), DinoArt::trexRunA().width(),
//                      DinoArt::trexRunA().height(), color);
//
// Розміри питати в самого MonoBitmap (width()/height()) - вони різні залежно
// від DINO_ASSET_TIER, і дублювати їх макросами означало б мати два джерела
// правди.
//
// Самі дані - у DinoSprites.cpp, який ГЕНЕРУЄТЬСЯ tools/dino_assets.py.

// --------------------------------------------------------------------------
// Вибір набору спрайтів
// --------------------------------------------------------------------------
//
// Тір обирається за ВИСОТОЮ екрана після ротації, на етапі компіляції (а не
// gc-sections), щоб невикористаний набір гарантовано не потрапив у прошивку.
//
// TFT_WIDTH/TFT_HEIGHT - це розміри ПАНЕЛІ, до ротації. При TFT_ROTATION 1/3
// екран лежить на боці, тому висотою стає TFT_WIDTH.
#ifndef DINO_ASSET_TIER
#if defined(TFT_ROTATION) && ((TFT_ROTATION == 1) || (TFT_ROTATION == 3))
#define DINO_SCREEN_H (TFT_WIDTH)
#else
#define DINO_SCREEN_H (TFT_HEIGHT)
#endif
#if DINO_SCREEN_H >= 120
#define DINO_ASSET_TIER 2 /* M: діно 44x47 */
#else
#define DINO_ASSET_TIER 1 /* S: діно 22x24 */
#endif
#endif

// Цілочисельний масштаб при виводі. Потрібен лише дуже великим екранам:
// на 480x480 діно заввишки 47 px виглядав би мурахою, а окремий третій набір
// асетів заради однієї плати не вартий того - nearest neighbour з цілим
// множником для піксель-арту виглядає рівно так, як задумано.
#ifndef DINO_SPRITE_SCALE
#if defined(DINO_SCREEN_H) && (DINO_SCREEN_H >= 400)
#define DINO_SPRITE_SCALE 3
#else
#define DINO_SPRITE_SCALE 1
#endif
#endif

namespace DinoArt {

const MonoBitmap &trexIdle();
const MonoBitmap &trexRunA();
const MonoBitmap &trexRunB();
const MonoBitmap &trexDead();
const MonoBitmap &cactusSmall();
const MonoBitmap &cactusLarge();
const MonoBitmap &cloud();
const MonoBitmap &pebbleA();
const MonoBitmap &pebbleB();

}  // namespace DinoArt
