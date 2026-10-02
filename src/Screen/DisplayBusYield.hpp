#pragma once

// YIELD_DISPLAY_BUS() - див. пояснення нижче. Одне визначення на весь src/:
// ним користуються і команди дисплея (src/main.cpp), і SD (src/Sd/).
//
//   void dumpSomething() {
//     YIELD_DISPLAY_BUS();  // до першої операції з карткою чи setRotation()
//     ...
//   }

#include "App/AppGlobals.hpp"

// ---------------------------------------------------------------------------
// YIELD_DISPLAY_BUS() - RAII-дужка, яка тимчасово віддає SPI-шину дисплея.
//
// НАВІЩО: loop() тримає транзакцію дисплея відкритою через УВЕСЬ кадр
// (display.startWrite() ... display.endWrite()), і саме всередині неї
// викликаються commandHandler.update() та mqtt.loop(). Тобто будь-яка
// консольна чи MQTT-команда виконується з-під відкритої транзакції.
//
// А SPIClass::beginTransaction() бере НЕ рекурсивний мьютекс paramLock з
// portMAX_DELAY (framework-arduinoespressif32, libraries/SPI/src/SPI.cpp).
// Другий take з того самого потоку - вічний дедлок. Плата не просто "не
// відповідає": вона коректно блокується на семафорі, тому процесор
// віддано, idle task живий і ЖОДЕН watchdog її не перезавантажить.
//
// Хто саме бере транзакцію вдруге:
//   - драйвер SD (libraries/SD/src/sd_diskio.cpp, struct AcquireSPI) -
//     на КОЖНУ операцію з карткою;
//   - сам Arduino_GFX - наприклад Arduino_ST7735::setRotation() робить
//     _bus->beginWrite(), тобто навіть звичайний flip екрана з команди.
//
// Асиметрія викликів нижче навмисна:
//   endWrite()   - безпечно викликати зайвий раз: SPIClass::endTransaction()
//                  захищений прапорцем _inTransaction;
//   startWrite() - НЕ можна двічі поспіль: Arduino_TFT::startWrite() не має
//                  лічильника вкладеності і напряму робить beginWrite().
//
// Відновлення - УМОВНЕ, за display.isWriting(). Ті самі функції
// викликаються і з-під транзакції (консольний flip усередині кадру), і
// поза нею (updateImuFlip() - вже після endWrite()). Безумовне
// відновлення залишило б транзакцію відкритою там, де її не було, і
// наступний startWrite() у loop() дав би той самий дедлок.
// ---------------------------------------------------------------------------
#if defined(DISPLAY_BUS_YIELD) && DISPLAY_BUS_YIELD
struct DisplayBusYield {
  const bool _wasWriting;
  DisplayBusYield() : _wasWriting(display.isWriting()) {
    if (_wasWriting) display.releaseBus();
  }
  ~DisplayBusYield() {
    if (_wasWriting) display.reacquireBus();
  }
};
#define YIELD_DISPLAY_BUS() DisplayBusYield _displayBusYield_
#else
#define YIELD_DISPLAY_BUS() ((void)0)
#endif
