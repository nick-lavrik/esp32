#pragma once

#if __has_include(<Adafruit_NeoPixel.h>)

#include <Adafruit_NeoPixel.h>

#define LED_PIN     8  // Пін керування вбудованим RGB LED
#define NUM_LEDS    1  // Кількість світлодіодів на платі

// Ініціалізація об'єкта світлодіода
Adafruit_NeoPixel strip(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);

void setup() {
  strip.begin();           // Ініціалізація NeoPixel
  strip.setBrightness(50); // Встановлення яскравості (0-255), не ставте на максимум, щоб не засліплювало
  strip.show();            // Вимкнути всі пікселі на старті
}

void loop() {
  // Увімкнути ЧЕРВОНИЙ колір: strip.Color(R, G, B)
  strip.setPixelColor(0, strip.Color(255, 0, 0)); 
  strip.show();
  delay(1000);

  // Увімкнути ЗЕЛЕНИЙ колір
  strip.setPixelColor(0, strip.Color(0, 255, 0));
  strip.show();
  delay(1000);

  // Увімкнути СИНІЙ колір
  strip.setPixelColor(0, strip.Color(0, 0, 255));
  strip.show();
  delay(1000);
}

#endif

#if !defined(RGB_BUILTIN)
void loopRgbLed() {}
#else
// Використовуємо вбудовану константу RGB_BUILTIN (для цієї плати це GPIO8)
// Якщо компілятор її не знає, можете замінити на: const int LED_PIN = 8;
#define LED_PIN RGB_BUILTIN 

void loopRgbLed() {
  // Параметри: rgbLedWrite(пін, Червоний, Зелений, Синій)
  // Значення кольору задаються від 0 (вимкнено) до 255 (максимум)

  // 1. Світимо ЧЕРВОНИМ
  Serial.println("rgbLedWrite('red')");
  rgbLedWrite(LED_PIN, 64, 0, 0); // 64 — помірна яскравість, щоб не сліпило
  delay(1000);

  // 2. Світимо ЗЕЛЕНИМ
  Serial.println("rgbLedWrite('blue')");
  rgbLedWrite(LED_PIN, 0, 64, 0);
  delay(1000);

  // 3. Світимо СИНІМ
  Serial.println("rgbLedWrite('green')");
  rgbLedWrite(LED_PIN, 0, 0, 64);
  delay(1000);

  // 4. Повністю ВИМИКАЄМО світлодіод
  Serial.println("rgbLedWrite(OFF)");
  rgbLedWrite(LED_PIN, 0, 0, 0);
  delay(1000);
}
#endif