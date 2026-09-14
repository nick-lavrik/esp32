#pragma once

#include <ESPAsyncWebServer.h>

#include "IStaticSource.hpp"

// Підключає IStaticSource до ESPAsyncWebServer як звичайний AsyncWebHandler.
//
// ЧОМУ НЕ "StaticRequestHandler", як звався клас раніше. Рівно так зветься
// внутрішній клас вбудованої в arduino-esp32 бібліотеки WebServer
// (libraries/WebServer/src/detail/RequestHandlersImpl.h), і вона потрапляє
// в збірку разом із lib/SDImageServer. Обидва класи лежали в глобальному
// namespace, тому в них збігалось манглене ім'я vtable
// (_ZTV20StaticRequestHandler) - лінкер мовчки лишав ОДНУ таблицю, чужу.
// Наслідок був не падінням лінкера, а гіршим: об'єкт створювався нашим
// конструктором, а canHandle() викликався з core-класу (інша сигнатура,
// той самий слот) і завжди повертав false - портал віддавав 404 на всю
// статику, не заходячи в наш код. Та сама пастка, через яку обгортка
// зветься HttpServer, а не WebServer (див. HttpServer.hpp).
//
// Перевірити ім'я на колізію перед додаванням нового публічного класу:
//   grep -rl "class <Name>\b" ~/.platformio/packages/framework-arduinoespressif32/libraries .pio/libdeps/<env>
// Не володіє staticSource - переданий ззовні (зазвичай CompositeStaticSource),
// його час життя контролює власник WebServer.
//
// SPA fallback: запит на "/" резолвиться в indexPath (за замовчуванням
// "/index.html"). Резолвінг вкладених SPA-роутів (наприклад "/settings"
// без розширення файлу -> теж "/index.html") тут НЕ реалізований - додати
// окремо, якщо знадобиться client-side routing.
class StaticSourceHandler : public AsyncWebHandler {
public:
  explicit StaticSourceHandler(IStaticSource* staticSource,
                                const String& indexPath = "/index.html")
      : _staticSource(staticSource), _indexPath(indexPath) {}

  bool canHandle(AsyncWebServerRequest* request) const override;
  void handleRequest(AsyncWebServerRequest* request) override;

private:
  IStaticSource* _staticSource;
  String _indexPath;

  String _resolvePath(const String& url) const;
};
