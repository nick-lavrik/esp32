#include "CommandQueue.hpp"

#include "CommandMask.hpp"

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
  slot.kind = Kind::kText;
  strncpy(slot.payload, line, kLineSize - 1);
  slot.payload[kLineSize - 1] = '\0';
  slot.reply = std::move(reply);

  _head = (_head + 1) % kSlots;
  ++_count;
  unlock();
  return true;
}

bool CommandQueue::submitJson(const JsonApiEntry* entry, uint32_t requestId, const uint8_t* args, size_t argsSize,
                               std::shared_ptr<ResponseTarget> reply) {
  if (entry == nullptr) return false;
  if (argsSize > kLineSize) {
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
  slot.kind = Kind::kJson;
  if (args != nullptr && argsSize > 0) {
    memcpy(slot.payload, args, argsSize);
  }
  slot.reply = std::move(reply);
  slot.jsonEntry = entry;
  slot.jsonRequestId = requestId;

  _head = (_head + 1) % kSlots;
  ++_count;
  unlock();
  return true;
}

bool CommandQueue::runNext() {
  Slot slot;

  lock();
  if (_count == 0) {
    unlock();
    return false;
  }
  // Копіюємо під замком і одразу звільняємо слот: сама команда виконується
  // довго (sdbench - десятки секунд), а submit()/submitJson() з таска сервера
  // не мають на неї чекати.
  slot = _slots[_tail];
  _slots[_tail] = Slot{};
  _tail = (_tail + 1) % kSlots;
  --_count;
  unlock();

  if (slot.kind == Kind::kJson) {
    runJsonNow(slot);
  } else {
    runNow(slot.payload, std::move(slot.reply));
  }
  return true;
}

void CommandQueue::runJsonNow(const Slot& slot) {
  const uint32_t startedMs = millis();

  String body = "{\"id\":" + String(slot.jsonRequestId) + ",\"ok\":true,\"data\":";
  body += slot.jsonEntry->execute(reinterpret_cast<const uint8_t*>(slot.payload));
  body += "}";

  if (slot.reply) {
    slot.reply->deliver(body.c_str(), body.length(), true);
  }

  _doneLogger.info("< json %s (%u ms)", slot.jsonEntry->name, (unsigned)(millis() - startedMs));
}

void CommandQueue::runNow(const char* line, std::shared_ptr<ResponseTarget> reply) {
  if (!_executor) {
    _logger.error("no executor - '%s' dropped", maskCommandSecrets(line).c_str());
    return;
  }

  const uint32_t startedMs = millis();
  // Для логу - лише замаскована копія: пароль з 'password <p>' не має
  // потрапити в журнал (див. CommandMask.hpp).
  const String shown = maskCommandSecrets(line);

  if (!reply) {
    _executor(line);
  } else {
    // Хендлери команд нічого не знають про відповідь: увесь їхній вивід іде
    // через TLogger, а CommandResponse підписаний у журналі на записи ЦЬОГО
    // таска (див. lib/CommandResponse).
    CommandResponse response(std::move(reply));
    if (!response.attach()) {
      _logger.error("no free journal slot - '%s' runs without a reply", shown.c_str());
    }

    // Луна команди - вже ПІСЛЯ підписки, щоб потрапила і в консоль, і у
    // відповідь: підписник reply-топіка бачить лише вивід і без неї не знав би,
    // на що саме цей вивід.
    _logger.info("> %s", shown.c_str());
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
  _doneLogger.info("< %s (%u ms)", shown.c_str(), (unsigned)(millis() - startedMs));
}

size_t CommandQueue::pending() const {
  lock();
  const size_t count = _count;
  unlock();
  return count;
}
