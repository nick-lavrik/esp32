#pragma once

// Стани FSM NetworkSupervisor.
//
// Діаграма переходів:
//
//   IDLE → SCANNING → CONNECTING → CONNECTED
//                  ↘              ↘ (disconnect, autoReconnect=true) → RECONNECTING → SCANNING
//                   ↘ (всі невдалі, wpsEnabled=true) → WPS_WAITING → CONNECTED
//                   ↘                                              ↘ (fail/timeout) → STARTING_AP
//                   ↘ (всі невдалі, wpsEnabled=false) → STARTING_AP → AP_MODE
//   AP_MODE → (scanInterval, autoReconnect=true) → SCANNING
//
//   startWps() можна викликати з будь-якого стану (ручний тригер).

enum class NetworkSupervisorState : uint8_t {
  IDLE,         // begin() ще не викликано
  SCANNING,     // сканування ефіру
  CONNECTING,   // спроба підключення до конкретної збереженої мережі
  CONNECTED,    // підключено, IP отримано
  RECONNECTING, // втрачено з'єднання, намагаємось відновити
  WPS_WAITING,  // очікуємо підтвердження WPS (кнопка на роутері або PIN)
  STARTING_AP,  // запускаємо точку доступу
  AP_MODE,      // працюємо як AP; чекаємо сканування або команди
};
