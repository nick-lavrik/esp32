#include "WebLogBuffer.hpp"

#include <cstring>

WebLogBuffer::WebLogBuffer() {
#if defined(ESP32)
  _mutex = xSemaphoreCreateMutex();
#endif
}

WebLogBuffer::~WebLogBuffer() {
#if defined(ESP32)
  if (_mutex) vSemaphoreDelete(_mutex);
#endif
}

void WebLogBuffer::_append(const char* line) {
  // Викликається вже під замком.
  char* slot = _lines[_head % kLines];
  strncpy(slot, line, kLineSize - 1);
  slot[kLineSize - 1] = '\0';
  ++_head;
}

size_t WebLogBuffer::write(uint8_t c) { return write(&c, 1); }

size_t WebLogBuffer::write(const uint8_t* buffer, size_t size) {
  Lock lock(_mutex);

  for (size_t i = 0; i < size; ++i) {
    const char c = static_cast<char>(buffer[i]);

    if (c == '\r') continue;  // CRLF від println() - '\r' у веб-консолі зайвий

    if (c == '\n') {
      _partial[_partialLength] = '\0';
      _append(_partial);
      _partialLength = 0;
      continue;
    }

    // Переповнення рядка: скидаємо те, що є, і продовжуємо в наступному.
    // Логер обрізає рядки по тій самій межі, тож у нормі сюди не доходить.
    if (_partialLength >= kLineSize - 1) {
      _partial[_partialLength] = '\0';
      _append(_partial);
      _partialLength = 0;
    }

    _partial[_partialLength++] = c;
  }

  return size;
}

void WebLogBuffer::push(const char* line) {
  if (line == nullptr) return;

  Lock lock(_mutex);
  _append(line);
}

uint32_t WebLogBuffer::head() const {
  Lock lock(const_cast<WebLogBuffer*>(this)->_mutex);
  return _head;
}

uint32_t WebLogBuffer::tail() const {
  Lock lock(const_cast<WebLogBuffer*>(this)->_mutex);
  return (_head > kLines) ? (_head - kLines) : 0;
}

bool WebLogBuffer::get(uint32_t seq, char* out, size_t outSize) const {
  if (out == nullptr || outSize == 0) return false;

  Lock lock(const_cast<WebLogBuffer*>(this)->_mutex);

  const uint32_t oldest = (_head > kLines) ? (_head - kLines) : 0;
  if (seq < oldest || seq >= _head) return false;

  strncpy(out, _lines[seq % kLines], outSize - 1);
  out[outSize - 1] = '\0';
  return true;
}
