#include "WebJobQueue.hpp"

WebJobQueue::WebJobQueue() {
#if defined(ESP32)
  _mutex = xSemaphoreCreateMutex();
#endif
}

WebJobQueue::~WebJobQueue() {
#if defined(ESP32)
  if (_mutex) vSemaphoreDelete(_mutex);
#endif
}

uint32_t WebJobQueue::submit(Handler handler) {
  if (!handler) return 0;

  Lock lock(_mutex);

  for (size_t i = 0; i < kSlots; ++i) {
    if (_slots[i].id != 0) continue;

    const uint32_t id = _nextId++;
    if (_nextId == 0) _nextId = 1;  // 0 зарезервований під "немає задачі"

    _slots[i].id = id;
    _slots[i].handler = std::move(handler);
    return id;
  }

  return 0;
}

WebJobQueue::Status WebJobQueue::status(uint32_t id, String& outResult) const {
  if (id == 0) return Status::UNKNOWN;

  Lock lock(const_cast<WebJobQueue*>(this)->_mutex);

  for (size_t i = 0; i < kResults; ++i) {
    if (_results[i].id == id) {
      outResult = _results[i].text;
      return Status::DONE;
    }
  }

  if (_runningId == id) return Status::RUNNING;

  for (size_t i = 0; i < kSlots; ++i) {
    if (_slots[i].id == id) return Status::QUEUED;
  }

  return Status::UNKNOWN;
}

void WebJobQueue::loop() {
  Handler handler;
  uint32_t id = 0;

  {
    Lock lock(_mutex);
    for (size_t i = 0; i < kSlots; ++i) {
      if (_slots[i].id == 0) continue;
      id = _slots[i].id;
      handler = std::move(_slots[i].handler);
      _slots[i] = Slot{};
      _runningId = id;
      break;
    }
  }

  if (id == 0) return;

  // Сам виклик - ПОЗА замком: задача триває секунди (скан ефіру, запис у NVS),
  // і тримати на цей час замок означало б заблокувати таск сервера на кожному
  // GET /api/job.
  String text = handler();

  {
    Lock lock(_mutex);
    _results[_nextResult] = Result{id, std::move(text)};
    _nextResult = (_nextResult + 1) % kResults;
    _runningId = 0;
  }
}

size_t WebJobQueue::pending() const {
  Lock lock(const_cast<WebJobQueue*>(this)->_mutex);

  size_t count = 0;
  for (size_t i = 0; i < kSlots; ++i) {
    if (_slots[i].id != 0) ++count;
  }
  return count;
}
