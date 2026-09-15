#include "CommandQueue.hpp"

#include <cstring>

#if defined(ESP32)

CommandQueue::CommandQueue() { _mutex = xSemaphoreCreateMutexStatic(&_mutexBuffer); }

void CommandQueue::lock() const { xSemaphoreTake(_mutex, portMAX_DELAY); }
void CommandQueue::unlock() const { xSemaphoreGive(_mutex); }

#else

// ESP8266: RTOS немає, loop() кооперативний - конкурувати нема кому (та сама
// логіка, що в Journal.cpp).
CommandQueue::CommandQueue() {}

void CommandQueue::lock() const {}
void CommandQueue::unlock() const {}

#endif

bool CommandQueue::submit(const char* line, std::shared_ptr<ResponseTarget> reply) {
  if (line == nullptr || line[0] == '\0') return false;
  if (strlen(line) >= kLineSize) {
    ++_rejected;
    return false;
  }

  lock();
  if (_count >= kSlots) {
    ++_rejected;
    unlock();
    return false;
  }

  Slot& slot = _slots[_head];
  strncpy(slot.line, line, kLineSize - 1);
  slot.line[kLineSize - 1] = '\0';
  slot.reply = std::move(reply);

  _head = (_head + 1) % kSlots;
  ++_count;
  unlock();
  return true;
}

bool CommandQueue::runNext() {
  char line[kLineSize];
  std::shared_ptr<ResponseTarget> reply;

  lock();
  if (_count == 0) {
    unlock();
    return false;
  }
  // Копіюємо під замком і одразу звільняємо слот: сама команда виконується
  // довго (sdbench - десятки секунд), а submit() з таска сервера не має на неї
  // чекати.
  memcpy(line, _slots[_tail].line, sizeof(line));
  reply = std::move(_slots[_tail].reply);
  _slots[_tail] = Slot{};
  _tail = (_tail + 1) % kSlots;
  --_count;
  unlock();

  runNow(line, std::move(reply));
  return true;
}

void CommandQueue::runNow(const char* line, std::shared_ptr<ResponseTarget> reply) {
  if (!_executor) {
    _logger.error("no executor - '%s' dropped", line != nullptr ? line : "");
    return;
  }

  const uint32_t startedMs = millis();

  if (!reply) {
    _executor(line);
  } else {
    // Хендлери команд нічого не знають про відповідь: увесь їхній вивід іде
    // через TLogger, а CommandResponse підписаний у журналі на записи ЦЬОГО
    // таска (див. lib/CommandResponse).
    CommandResponse response(std::move(reply));
    if (!response.attach()) {
      _logger.error("no free journal slot - '%s' runs without a reply", line);
    }

    // Луна команди - вже ПІСЛЯ підписки, щоб потрапила і в консоль, і у
    // відповідь: підписник reply-топіка бачить лише вивід і без неї не знав би,
    // на що саме цей вивід.
    _logger.info("> %s", line);
    _executor(line);

    response.finish();
  }

  // Явний кінець команди, парний до луни "> ...". Потрібен не для краси:
  // вивід команди йде звичайним логом, упереміш із рядками фонових тасків і
  // крону, і ззовні неможливо сказати, де він закінчився. Веб-портал саме по
  // цьому рядку перестає дописувати панель Output (див. lib/WebPortal), а в
  // serial-моніторі видно, скільки команда справді працювала.
  //
  // ПІСЛЯ response.finish() навмисно: у відповідь на MQTT чи в лист має піти
  // рівно вивід команди, без нашої службової позначки.
  _doneLogger.info("< %s (%u ms)", line, (unsigned)(millis() - startedMs));
}

size_t CommandQueue::pending() const {
  lock();
  const size_t count = _count;
  unlock();
  return count;
}
