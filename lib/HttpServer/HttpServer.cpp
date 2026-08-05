#include "HttpServer.hpp"

#include <LittleFS.h>

#include "HttpServerStartedEvent.hpp"
#include "HttpServerStoppedEvent.hpp"
#include "IEventDispatcher.hpp"
#include "IStaticSource.hpp"
#include "StaticRequestHandler.hpp"
// ITemplateResolver ще не реалізований (наступний крок) - setTemplateProcessor()
// підключимо до StaticRequestHandler/AsyncFileResponse пізніше.
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

bool HttpServer::begin() {
  if (_isRunning) {
    return true;
  }

  if (_staticSource != nullptr) {
    // addHandler бере вказівник у std::unique_ptr всередині AsyncWebServer -
    // видаляти самостійно не потрібно.
    _server.addHandler(new StaticRequestHandler(_staticSource));
  }

  _server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");

  // Без цього AsyncWebServer::_catchAllHandler віддає 500 для будь-якого
  // запиту, для якого жоден handler не спрацював (canHandle() == false
  // у всіх). Явний onNotFound перетворює це на очікуваний 404.
  _server.onNotFound(
      [](AsyncWebServerRequest* request) { request->send(404, "text/plain", "Not found"); });

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
