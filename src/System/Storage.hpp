#pragma once

// Файлова система й NVS - обидві потрібні до setupSerialCommander():
// команди читають конфіг під час реєстрації.
void setupLittleFS();
void setupConfigStorage();
