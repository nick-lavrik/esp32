#pragma once

// Стани FSM NetworkManager.
//
// Діаграма переходів:
//   IDLE → SCANNING → CONNECTING → CONNECTED
//                               ↘ (всі спроби невдалі) → STARTING_AP → AP_MODE
//   CONNECTED → (disconnect) → RECONNECTING → SCANNING  (якщо autoReconnect=true)
//   AP_MODE   → (scanInterval elapsed, autoReconnect=true) → SCANNING
enum class NetworkManagerState : uint8_t {
  IDLE,         // begin() ще не викликано
  SCANNING,     // сканування ефіру
  CONNECTING,   // спроба підключення до конкретної мережі
  CONNECTED,    // підключено, IP отримано
  RECONNECTING, // втрачено з'єднання, намагаємось відновити
  STARTING_AP,  // запускаємо точку доступу
  AP_MODE,      // працюємо як AP, чекаємо або сканування, або команди
};
