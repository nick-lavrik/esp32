#pragma once

// Розділ "screen": дзеркало фізичного екрана у браузері.
//
// Пристрій НЕ збирає повний кадр. Display малює його смугами
// (DISPLAY_SPLIT_COUNT) і виштовхує кожну смугу в панель окремо; сюди
// потрапляє знімок рівно тієї смуги, а повний екран збирає браузер у
// <canvas> - тобто дублює display.flush() на клієнті. Механіка знімка й уся
// синхронізація живуть у lib/ScreenMirror, тут лише транспорт.
//
// Темп оновлення задає КЛІЄНТ. Смуга знімається рівно раз на HTTP-запит, а
// не щокадру, тож поки вкладку не відкрито (або натиснуто Pause) плата не
// робить нічого зайвого - жодного прапорця "скільки кадрів пропускати" для
// цього не потрібно. А що тягне канал, те й буде FPS: наступний запит іде
// лише після того, як приїхав попередній (poller() на сторінці).
//
// Чому не WebJobQueue. Читання буфера смуги - це memcpy з RAM, воно не чіпає
// ні flash, ні WiFi, ні шину дисплея, тобто в таску сервера цілком доречне.
// Черга тут лише додала б затримку в кадр і зайняла слот, потрібний реальним
// мутаціям.

#include <Arduino.h>
#include <ESPAsyncWebServer.h>

#include <ScreenMirror.hpp>

#include "IWebModule.hpp"

class WebScreenModule : public IWebModule {
public:
  const char* name() const override { return "screen"; }
  void registerRoutes(AsyncWebServer& server, WebPortal& portal) override;

  // Таймаут простою дзеркала: звільнити буфер смуги, коли вкладку закрили.
  void loop() override { ScreenMirror::instance().tick(millis()); }
};
