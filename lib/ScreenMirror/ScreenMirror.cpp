#include "ScreenMirror.hpp"

#include <TLogger.hpp>
#include <stdlib.h>

namespace {

// Скільки чекати, перш ніж звільнити буфер. Вкладку закрили - пам'ять має
// повернутись у купу: на C6 це 13-27 КБ з приблизно сотні вільних.
constexpr uint32_t kIdleTimeoutMs = 3000;

// Скільки чекати завершення віддачі. Клієнт може відвалитись посеред
// відповіді (закрив вкладку, впав лінк) - тоді останній шматок ніхто не
// забере і release() не настане. Без цього таймауту дзеркало залипло б у
// Draining назавжди.
constexpr uint32_t kDrainTimeoutMs = 4000;

const TLogger logger{"scr"};

// RGB565 -> RGB332: беремо старші 3/3/2 біти. Молодші відкидаються, тому
// градієнти стають ступінчастими - це і є ціна удвічі меншого трафіку.
inline uint8_t rgb565to332(uint16_t c) {
  return static_cast<uint8_t>(((c >> 8) & 0xE0) | ((c >> 6) & 0x1C) | ((c >> 3) & 0x03));
}

inline uint16_t bswap16(uint16_t v) { return static_cast<uint16_t>((v >> 8) | (v << 8)); }

}  // namespace

ScreenMirror& ScreenMirror::instance() {
  static ScreenMirror mirror;
  return mirror;
}

void ScreenMirror::begin(uint16_t width, uint16_t height, uint8_t splitCount, uint16_t splitHeight,
                         bool swapped565) {
  _width = width;
  _height = height;
  _splitCount = splitCount > 0 ? splitCount : 1;
  _splitHeight = splitHeight;
  _swapped565 = swapped565;
  _nextIndex = 0;
  logger.info("mirror ready: %ux%u, %u strip(s) of %u px, %s", (unsigned)_width, (unsigned)_height,
              (unsigned)_splitCount, (unsigned)_splitHeight, swapped565 ? "big-endian" : "little-endian");
}

void ScreenMirror::arm(Format format) {
  if (!available()) return;
  _wanted.store(static_cast<uint8_t>(format), std::memory_order_relaxed);
  _lastUseMs.store(millis(), std::memory_order_relaxed);

  // Armed виставляємо лише з Idle. Решта станів означає, що знімок або вже
  // готується, або лежить готовий, або віддається - переривати нічого не треба.
  uint8_t expected = Idle;
  _state.compare_exchange_strong(expected, Armed, std::memory_order_acq_rel, std::memory_order_relaxed);
}

bool ScreenMirror::take(Format format, Snapshot& out) {
  _lastUseMs.store(millis(), std::memory_order_relaxed);

  uint8_t expected = Ready;
  if (!_state.compare_exchange_strong(expected, Draining, std::memory_order_acq_rel,
                                      std::memory_order_relaxed)) {
    arm(format);
    return false;
  }

  // Готовий знімок іншого формату (клієнт щойно перемкнув перемикач) - не
  // віддаємо: браузер розібрав би байти не так. Повертаємо буфер у роботу.
  if (_format != format) {
    _state.store(Idle, std::memory_order_release);
    arm(format);
    return false;
  }

  out.data = _buffer;
  out.size = _size;
  out.width = _width;
  out.height = _splitHeight;
  out.index = _index;
  out.splitCount = _splitCount;
  out.frame = _frame;
  out.format = _format;
  _drainStartedMs.store(millis(), std::memory_order_relaxed);
  return true;
}

void ScreenMirror::release(uint32_t frame) {
  // Чужий кадр - значить, цю віддачу вже хтось завершив (або її розчепив
  // таймаут), а Draining зараз тримає ІНШИЙ запит. Чіпати його не можна:
  // loop() почав би писати в буфер, який той саме читає.
  if (_frame != frame) return;

  uint8_t expected = Draining;
  if (!_state.compare_exchange_strong(expected, Idle, std::memory_order_acq_rel,
                                      std::memory_order_relaxed)) {
    return;  // таймаут tick() уже розчепив - нічого робити
  }
  // Наступну смугу просимо одразу: поки браузер розбирає цю, плата вже має
  // напоготові свіжу, і наступний запит не чекає жодного кадру.
  arm(static_cast<Format>(_wanted.load(std::memory_order_relaxed)));
}

void ScreenMirror::capture(const uint16_t* strip, uint8_t splitIndex) {
  if (strip == nullptr) return;

  // Звичайний випадок - ніхто не просив: один atomic load на кадр.
  if (_state.load(std::memory_order_relaxed) != Armed) return;

  // Смуги віддаються ПО КОЛУ: наступна завжди (попередня + 1) % splitCount.
  // Без цього ми знімали б ту смугу, яка трапилась під рукою, і повільний
  // клієнт міг би раз за разом отримувати одну й ту саму: період опитування
  // легко потрапляє в резонанс з періодом кадру, і тоді половина екрана в
  // браузері просто не оновлюється. Ціна - чекання до splitCount-1 ітерацій
  // loop() на потрібну смугу; поки клієнт встигає за платою, чекати нема чого,
  // бо наступна ітерація і малює саме наступну смугу.
  if (splitIndex != _nextIndex) return;

  uint8_t expected = Armed;
  if (!_state.compare_exchange_strong(expected, Capturing, std::memory_order_acq_rel,
                                      std::memory_order_relaxed)) {
    return;
  }

  const Format format = static_cast<Format>(_wanted.load(std::memory_order_relaxed));
  const size_t pixels = static_cast<size_t>(_width) * _splitHeight;
  const size_t bytes = format == Format::Rgb565 ? pixels * 2 : pixels;

  if (!_ensureBuffer(bytes)) {
    _state.store(Idle, std::memory_order_release);
    return;
  }

  if (format == Format::Rgb565) {
    memcpy(_buffer, strip, bytes);  // порядок байтів віддаємо як є, клієнту він відомий
  } else if (_swapped565) {
    for (size_t i = 0; i < pixels; i++) _buffer[i] = rgb565to332(bswap16(strip[i]));
  } else {
    for (size_t i = 0; i < pixels; i++) _buffer[i] = rgb565to332(strip[i]);
  }

  _size = bytes;
  _index = splitIndex;
  _nextIndex = static_cast<uint8_t>((splitIndex + 1) % _splitCount);
  _format = format;
  _frame++;
  _state.store(Ready, std::memory_order_release);
}

void ScreenMirror::tick(uint32_t nowMs) {
  if (_buffer == nullptr) return;

  const uint32_t idleMs = nowMs - _lastUseMs.load(std::memory_order_relaxed);
  uint8_t state = _state.load(std::memory_order_acquire);

  if (state == Draining) {
    // Рахуємо від ПОЧАТКУ віддачі, а не від останнього запиту: клієнт, який
    // далі опитує (і отримує 204), інакше нескінченно відсував би цей таймаут,
    // і одна обірвана відповідь вішала б дзеркало назавжди. Саме так воно й
    // повелось після першої ж перерваної відповіді.
    if (nowMs - _drainStartedMs.load(std::memory_order_relaxed) < kDrainTimeoutMs) return;
    uint8_t expected = Draining;
    if (_state.compare_exchange_strong(expected, Idle, std::memory_order_acq_rel,
                                       std::memory_order_relaxed)) {
      logger.warn("client vanished mid-frame, buffer released");
      _freeBuffer();
    }
    return;
  }

  if (idleMs < kIdleTimeoutMs) return;

  // Звільняти буфер можна лише з Idle - інакше його або саме зараз пише
  // capture(), або вже читає таск сервера.
  uint8_t expected = Ready;
  if (!_state.compare_exchange_strong(expected, Idle, std::memory_order_acq_rel,
                                      std::memory_order_relaxed)) {
    expected = Armed;
    if (!_state.compare_exchange_strong(expected, Idle, std::memory_order_acq_rel,
                                        std::memory_order_relaxed)) {
      return;
    }
  }
  _freeBuffer();
}

bool ScreenMirror::_ensureBuffer(size_t bytes) {
  if (_buffer != nullptr && _capacity >= bytes) return true;
  _freeBuffer();

#if defined(ESP32) && defined(BOARD_HAS_PSRAM)
  // Плата з PSRAM - беремо смугу туди: на 480x480 (env:esp32-4848s040) вона
  // важить 460 КБ і у внутрішню купу не влізе в принципі.
  _buffer = static_cast<uint8_t*>(ps_malloc(bytes));
  if (_buffer == nullptr)
#endif
    _buffer = static_cast<uint8_t*>(malloc(bytes));

  if (_buffer == nullptr) {
    logger.error("out of memory: %u bytes for one strip", (unsigned)bytes);
    _capacity = 0;
    return false;
  }
  _capacity = bytes;
  return true;
}

void ScreenMirror::_freeBuffer() {
  free(_buffer);
  _buffer = nullptr;
  _capacity = 0;
  _size = 0;
}
