#include "CommandResponse.hpp"

#include <cstdio>

#include <ScopedLogCapture.hpp>

namespace {
// Запас під маркер обрізання - "...(truncated, 4294967295 more lines)\n".
constexpr size_t kTruncationMarkerMax = 48;
}  // namespace

CommandResponse::CommandResponse(std::shared_ptr<ResponseTarget> target) : _target(std::move(target)) {}

CommandResponse::~CommandResponse() { finish(); }

size_t CommandResponse::write(uint8_t c) { return write(&c, 1); }

size_t CommandResponse::write(const uint8_t* buffer, size_t size) {
  if (_finished || !_target) {
    return size;  // Print очікує кількість "записаних" байтів
  }

  for (size_t i = 0; i < size; ++i) {
    const char c = static_cast<char>(buffer[i]);

    // Ліміт вичерпано: далі лише рахуємо втрачені рядки, щоб сказати про це
    // в маркері. Дає до kMaxChunks порцій тексту плюс короткий фінальний
    // маркер - без цього команда, що зациклилась у виводі, забила б брокер.
    if (_chunksSent >= kMaxChunks) {
      if (c == '\n') {
        ++_droppedLines;
      }
      continue;
    }

    // Місця не лишилось навіть під один символ (з резервом під '\0') - рвемо
    // порцію тут. Спрацьовує лише для джерела з рядками, довшими за
    // PrintQueueLineSize; для логера порцію рве гілка нижче, по межі рядка.
    if (_length + 1 >= kChunkBytes) {
      flushChunk(false);
    }

    _buffer[_length++] = c;

    // Рвемо по межі рядка, поки в буфері ще гарантовано влазить наступний
    // рядок логу - так жоден рядок не розрізається навпіл.
    if (c == '\n' && full()) {
      flushChunk(false);
    }
  }

  return size;
}

void CommandResponse::finish() {
  if (_finished) {
    return;
  }
  _finished = true;

  if (_droppedLines > 0) {
    // Маркер має піти обов'язково: якщо в буфері немає під нього місця -
    // спершу віддаємо накопичене окремою порцією.
    if (_length + kTruncationMarkerMax + 1 >= kChunkBytes) {
      flushChunk(false);
    }

    const int written = snprintf(_buffer + _length, kChunkBytes - _length,
                                 "...(truncated, %u more lines)\n", static_cast<unsigned>(_droppedLines));
    if (written > 0) {
      const size_t available = kChunkBytes - _length - 1;
      _length += (static_cast<size_t>(written) > available) ? available : static_cast<size_t>(written);
    }
  }

  flushChunk(true);
}

void CommandResponse::flushChunk(bool isFinal) {
  if (!_target) {
    _length = 0;
    return;
  }

  // Порожню порцію шлемо лише як фінальну - інакше приймач не дізнається, що
  // відповідь завершена (для EmailTarget це момент відправки листа).
  if (_length == 0 && !isFinal) {
    return;
  }

  _buffer[_length] = '\0';

  // Re-entrancy guard: доставка сама логує (напр. MqttClient::publish пише
  // warn при переповненні черги), і без зняття захоплення ці рядки пішли б
  // назад у цей самий буфер - нескінченна рекурсія.
  {
    ScopedLogCapture guard(nullptr);
    _target->deliver(_buffer, _length, isFinal);
  }

  _length = 0;
  ++_chunksSent;
}
