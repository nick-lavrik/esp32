#pragma once

#include <ESPAsyncWebServer.h>

#include "IStaticSource.hpp"

// Підключає IStaticSource до ESPAsyncWebServer як звичайний AsyncWebHandler.
// Не володіє staticSource - переданий ззовні (зазвичай CompositeStaticSource),
// його час життя контролює власник WebServer.
//
// SPA fallback: запит на "/" резолвиться в indexPath (за замовчуванням
// "/index.html"). Резолвінг вкладених SPA-роутів (наприклад "/settings"
// без розширення файлу -> теж "/index.html") тут НЕ реалізований - додати
// окремо, якщо знадобиться client-side routing.
class StaticRequestHandler : public AsyncWebHandler {
public:
  explicit StaticRequestHandler(IStaticSource* staticSource,
                                const String& indexPath = "/index.html")
      : _staticSource(staticSource), _indexPath(indexPath) {}

  bool canHandle(AsyncWebServerRequest* request) const override;
  void handleRequest(AsyncWebServerRequest* request) override;

private:
  IStaticSource* _staticSource;
  String _indexPath;

  String _resolvePath(const String& url) const;
};
