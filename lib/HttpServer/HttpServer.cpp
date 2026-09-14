#include "HttpServer.hpp"

#include "HttpServerStartedEvent.hpp"
#include "HttpServerStoppedEvent.hpp"
#include "IEventDispatcher.hpp"
#include "IStaticSource.hpp"
#include "StaticSourceHandler.hpp"
// ITemplateResolver ще не реалізований (наступний крок) - setTemplateProcessor()
// підключимо до StaticSourceHandler/AsyncFileResponse пізніше.
// #include "ITemplateResolver.hpp"

HttpServer::HttpServer(const HttpServerConfig& config) : _config(config), _server(config.port) {}

HttpServer::~HttpServer() { end(); }

void HttpServer::setStaticSource(IStaticSource* staticSource) { _staticSource = staticSource; }

void HttpServer::setTemplateResolver(ITemplateResolver* templateResolver) {
  _templateResolver = templateResolver;
}

void HttpServer::setEventDispatcher(IEventDispatcher* eventDispatcher) {
  _eventDispatcher = eventDispatcher;
}

void HttpServer::setAuth(const String& username, const String& password) {
  _authEnabled = username.length() > 0 && password.length() > 0;
  if (!_authEnabled) return;

  _auth.setUsername(username.c_str());
  _auth.setPassword(password.c_str());
  _auth.setAuthType(AsyncAuthType::AUTH_BASIC);
  _auth.setRealm("ESP32");
  _auth.setAuthFailureMessage("Authentication required");
}

bool HttpServer::hasAuth() const { return _authEnabled; }

bool HttpServer::begin() {
  if (_isRunning) {
    return true;
  }

  if (!_handlersRegistered) {
    // Auth - middleware рівня сервера, тобто накриває і статику, і всі роути,
    // зареєстровані модулями через server(). Додається ПЕРШИМ: інакше
    // запит устиг би дійти до handler-а до перевірки.
    if (_authEnabled) {
      _server.addMiddleware(&_auth);
    }

    if (_staticSource != nullptr) {
      // addHandler бере вказівник у std::unique_ptr всередині AsyncWebServer -
      // видаляти самостійно не потрібно.
      //
      // serveStatic(LittleFS) тут НЕ дублюємо: LittleFS - лише одне з
      // можливих джерел, і воно підключається ззовні через
      // LittleFsStaticSource. Дубль давав би два шляхи віддачі того самого
      // файлу з різною поведінкою (зокрема повз ProgmemStaticSource-fallback).
      _server.addHandler(new StaticSourceHandler(_staticSource));
    }

    // Без цього AsyncWebServer::_catchAllHandler віддає 500 для будь-якого
    // запиту, для якого жоден handler не спрацював (canHandle() == false
    // у всіх). Явний onNotFound перетворює це на очікуваний 404.
    _server.onNotFound(
        [](AsyncWebServerRequest* request) { request->send(404, "text/plain", "Not found"); });

    _handlersRegistered = true;
  }

  _server.begin();
  _isRunning = true;

  if (_eventDispatcher != nullptr) {
    HttpServerStartedEvent event(_config.port, _config.maxClients);
    _eventDispatcher->dispatch(event, HttpServerStartedEvent::kEventName);
  }

  return true;
}

void HttpServer::end() {
  if (!_isRunning) {
    return;
  }

  _server.end();
  _isRunning = false;

  if (_eventDispatcher != nullptr) {
    HttpServerStoppedEvent event;
    _eventDispatcher->dispatch(event, HttpServerStoppedEvent::kEventName);
  }
}
