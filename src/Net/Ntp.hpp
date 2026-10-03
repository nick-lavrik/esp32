#pragma once

// NTP: часовий пояс Europe/Kyiv, три сервери, лог на кожну синхронізацію.
// Неблокуючий - синхронізація відбувається у фоні, ntp.isSynced() скаже коли.
void setupNtpService();
