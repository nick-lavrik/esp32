#pragma once

// Команди 'net' (керування NetworkSupervisor у стилі nmcli) і 'scan'.
// Опис синтаксису й де живе конфігурація - на початку NetCli.cpp.

#include <stddef.h>

class NetworkSupervisor;
class SerialCommander;

// Засів списку профілів файлами з LittleFS. Викликати ПІСЛЯ ns.loadConfig().
// Повертає кількість прочитаних профілів (0 — каталогу немає або він порожній).
size_t importNetProfilesFromFs(NetworkSupervisor& ns);

// 'net' і 'scan' - над глобальним netSupervisor.
void registerNetCommands(SerialCommander& commander);
