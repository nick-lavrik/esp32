#pragma once

// Команда 'journal' (псевдонім 'log') - вікно в шину логу: приймачі, статистика
// кільця, tail і рівні за тегом. Подробиці - на початку JournalCli.cpp.

class SerialCommander;

void registerJournalCommands(SerialCommander& commander);
