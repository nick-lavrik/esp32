#include "Storage.hpp"

#include <LittleFS.h>

#include <Logger.hpp>

#include "App/AppGlobals.hpp"

void setupLittleFS() {
#if defined(ESP8266)
  bool mounted = LittleFS.begin();
#else
  bool mounted = LittleFS.begin(true);
#endif

  if (!mounted) {
    Logger::error("LittleFS mount failed!");
  } else {
    Logger::info("LittleFS mounted successfully (done)");
  }
}

void setupConfigStorage() {
  configStorage.begin(PIO_PIOENV);
  Logger::info("ConfigStorage init done");
}
