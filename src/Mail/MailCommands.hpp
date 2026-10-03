#pragma once

// Пошта (HAS_GMAIL_SENDER, lib/GmailSender): sendmail - дим-тест SMTP,
// mailto <address> <command> - вивід команди листом, smtp-probe [port] -
// конект до SMTP без поштової бібліотеки. Без HAS_GMAIL_SENDER команд немає.

#include <SerialCommander.hpp>

#include "features.h"

#if HAS_GMAIL_SENDER
void registerMailCommands(SerialCommander& commander);
#else
inline void registerMailCommands(SerialCommander&) {}
#endif
