#pragma once

// Обв'язка EcoFlow у застосунку: колбеки EcoflowClient/EcoflowDeviceRegistry
// (лог, MQTT-дзеркало grid), cron-задачі (live-чекпоінт, expireStale, REST ->
// MQTT на старті, 'ecoflow' раз на хвилину) і serial-команди ecoflow-*.
//
//   #if HAS_ECOFLOW_CLIENT
//   setupEcoflow();  // з setup(), після setupConfigStorage(), setupNtpService()
//   #endif           // і setupMqttClient()
//
// Самі клієнт і реєстр - глобали ecoflow/ecoflowDevices (src/App/AppGlobals.cpp).

#include "features.h"

#if HAS_ECOFLOW_CLIENT
void setupEcoflow();
#endif
