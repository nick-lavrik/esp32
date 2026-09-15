#pragma once

// Дзеркало екрана: знімок ОДНІЄЇ смуги кадру для веб-порталу.
//
// Повного фреймбуфера в проєкті немає й не треба: Display малює кадр
// смугами через спрайт розміром width x (height / DISPLAY_SPLIT_COUNT)
// і виштовхує кожну смугу в панель окремо (див. Display::flush()). Саме
// цю смугу ми й віддаємо назовні разом з її індексом, а повний екран
// збирає браузер у <canvas> - тобто дублює flush() на клієнті.
// Ціна питання: 13 КБ на C6 (172x320, split=4) замість 110 КБ на кадр.
//
// Хто кого кличе:
//
//   loop()  -> Display::flush() -> capture()   копіює смугу, якщо її просили
//   AsyncTCP -> arm()/take()/release()         запит і видача знімка
//   loop()  -> tick()                          таймаут простою, звільнення буфера
//
// Синхронізація - один atomic-стан і CAS-переходи, без мьютексів:
//
//   Idle --arm()--> Armed --capture()--> Capturing --> Ready --take()--> Draining
//     ^                                                                     |
//     +---------------------- tick() (таймаут) ------- release() -----------+
//
// Буфер один, і це навмисно: поки триває віддача (Draining), loop() у нього
// не пише, тобто рваного кадру не буває взагалі. Смуги при цьому йдуть ПО
// КОЛУ - (попередня + 1) % splitCount, - інакше повільний клієнт міг би раз за
// разом отримувати ту саму смугу, і решта екрана в браузері не оновлювалась би. Темп оновлення при цьому
// задає не пристрій, а клієнт: release() одразу ставить наступний запит,
// смуга знімається рівно раз на HTTP-запит, а не щокадру. Пауза у вкладці
// означає, що плата взагалі не робить зайвої роботи, а "кожен XX кадр"
// виходить саме собою - який темп тягне канал, такий і буде.

#include <Arduino.h>

#include <atomic>

class ScreenMirror {
public:
  // Формат байтів на дроті. RGB332 - удвічі менший трафік (і, відповідно,
  // удвічі вищий FPS) ціною 8-бітної палітри; RGB565 - те саме, що бачить
  // панель, піксель у піксель.
  enum class Format : uint8_t { Rgb565 = 0, Rgb332 = 1 };

  // Те, що віддається одним HTTP-запитом.
  struct Snapshot {
    const uint8_t* data = nullptr;
    size_t size = 0;
    uint16_t width = 0;
    uint16_t height = 0;   // висота СМУГИ, не екрана
    uint8_t index = 0;     // індекс смуги: 0 - верхня
    uint8_t splitCount = 1;
    uint32_t frame = 0;
    Format format = Format::Rgb332;
  };

  static ScreenMirror& instance();

  // Викликає Display::init() після створення спрайта. swapped565 - чи лежить
  // у спрайті старший байт першим (так робить LovyanGFX; TFT_eSPI і
  // Arduino_GFX тримають рідний little-endian).
  void begin(uint16_t width, uint16_t height, uint8_t splitCount, uint16_t splitHeight,
             bool swapped565);

  bool available() const { return _splitHeight > 0 && _width > 0; }

  uint16_t width() const { return _width; }
  uint16_t height() const { return _height; }
  uint8_t splitCount() const { return _splitCount; }
  uint16_t splitHeight() const { return _splitHeight; }
  bool swapped565() const { return _swapped565; }

  // ---- бік таску сервера ----

  // Просить наступну смугу у вказаному форматі. Безпечно кликати повторно.
  void arm(Format format);

  // Забирає готовий знімок. false - ще не готовий (або готовий у ІНШОМУ
  // форматі, ніж просять зараз); у цьому випадку запит уже переставлено,
  // клієнту лишається прийти наступним тіком.
  bool take(Format format, Snapshot& out);

  // Знімок віддано (або з'єднання обірвалось) - буфер вільний, одразу просимо
  // наступну смугу. frame - номер із самого знімка: він не дає запізнілому
  // onDisconnect() від СТАРОГО запиту розчепити віддачу, яка вже триває для
  // нового.
  void release(uint32_t frame);

  // ---- бік loop() ----

  // Копіює смугу зі спрайта, якщо її просили. Кличеться ЩОКАДРУ, тому
  // "не просили" має коштувати один atomic load.
  void capture(const uint16_t* strip, uint8_t splitIndex);

  // Таймаут простою: звільняє буфер, коли вкладку закрили, і розчіпляє
  // Draining, коли клієнт відвалився посеред відповіді.
  void tick(uint32_t nowMs);

private:
  ScreenMirror() = default;

  enum State : uint8_t { Idle = 0, Armed, Capturing, Ready, Draining };

  bool _ensureBuffer(size_t bytes);
  void _freeBuffer();

  std::atomic<uint8_t> _state{Idle};
  std::atomic<uint8_t> _wanted{static_cast<uint8_t>(Format::Rgb332)};
  std::atomic<uint32_t> _lastUseMs{0};
  // Момент початку віддачі. Окремо від _lastUseMs навмисно: той оновлює КОЖЕН
  // запит, і поки клієнт опитує, таймаут віддачі з нього не настав би ніколи -
  // а саме тоді він і потрібен (див. tick()).
  std::atomic<uint32_t> _drainStartedMs{0};

  uint8_t* _buffer = nullptr;
  size_t _capacity = 0;

  // Готовий знімок. Пишеться в loop() ДО переходу в Ready, читається в
  // таску сервера ПІСЛЯ нього - тобто перевпорядкування не страшне.
  size_t _size = 0;
  uint32_t _frame = 0;
  uint8_t _index = 0;
  // Яку смугу знімаємо наступною. Чіпає лише loop(), тому без atomic.
  uint8_t _nextIndex = 0;
  Format _format = Format::Rgb332;

  uint16_t _width = 0;
  uint16_t _height = 0;
  uint16_t _splitHeight = 0;
  uint8_t _splitCount = 1;
  bool _swapped565 = false;
};
