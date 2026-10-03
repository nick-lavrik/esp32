#pragma once

// Веб-портал (HAS_WEB_PORTAL, lib/WebPortal): реєстрація розділів і старт
// сервера, команда 'web status|auth'. Розділи - глобали з AppGlobals.cpp.
//
//   setupWebPortal();  // після setupNetworkSupervisor()

#include <SerialCommander.hpp>

#include "features.h"

#if HAS_WEB_PORTAL
void setupWebPortal();
void registerWebCommands(SerialCommander& commander);
#else
inline void registerWebCommands(SerialCommander&) {}
#endif
