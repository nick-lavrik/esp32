#pragma once

// Пристрої вводу: шина I2C, IMU, тачскрін, кнопки (PRIMARY/SECONDARY_BUTTON_PIN).
//
//   setupI2C();          // ДО setupTouchScreen()/setupImu() - шина спільна
//   setupTouchScreen();
//   setupImu();
//   setupButtons();   // події йдуть активному екрану - з loop(), не з setup()
//   updateImuFlip();     // щоітерації loop(), після кадру
//
// Кожна функція без свого заліза (I2C_SDA/BOARD_HAS_IMU/...) - порожня або
// лише логує, тож виклики лишаються без #if.

#include <SerialCommander.hpp>

void setupI2C();
void i2cScan();
void setupImu();
void updateImuFlip();
void setupTouchScreen();
void setupButtons();

// i2cscan (з I2C_SDA/I2C_SCL), imu (з BOARD_HAS_IMU).
void registerI2cCommands(SerialCommander& commander);
void registerImuCommands(SerialCommander& commander);
inline void registerInputCommands(SerialCommander& commander) {
  registerI2cCommands(commander);
  registerImuCommands(commander);
}
