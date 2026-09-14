#include "CommandResponse.hpp"

#include <Arduino.h>

#include <cstdio>
#include <cstring>

namespace {
// Запас під маркер обрізання - "...(truncated, 4294967295 more lines)\n".
constexpr size_t kTruncationMarkerMax = 48;
}  // namespace

CommandResponse::CommandResponse(std::shared_ptr<ResponseTarget> target) : _target(std::move(target)) {}

CommandResponse::~CommandResponse() { finish(); }

bool CommandResponse::attach() {
  if (_sub != kInvalidJournalSub) return true;

#if defined(ESP32)
  _task = pcTaskGetName(nullptr);
#else
  _task = "loop";  // RTOS немає, тасків теж - усе живе в loop()
#endif

  // Точка відліку. Підписка в журналі навмисно починається з ХВОСТА кільця
  // (щоб приймач, який щойно з'явився, не втратив уже залогованого - див.
  // Journal::subscribe), і для дзеркал це правильно. Для відповіді на команду -
  // ні: без цієї межі у відповідь спершу заїжджали всі 32 записи, що лежали в
  // кільці ДО команди. На залізі це виглядало так: у reply-топік приходило
  // чотири зайві порції зі станом мережі й ecoflow, і аж у п'ятій - луна
  // команди та її власний вивід.
  _from = Journal::instance().head();

  _sub = Journal::instance().subscribe(
      "cmd-reply", "", LogLevel::Verbose,
      [this](const JournalEntry& entry) { return _deliver(entry); }, /*lossless=*/true);
  return _sub != kInvalidJournalSub;
}

bool CommandResponse::_deliver(const JournalEntry& entry) {
  if (_finished || !_target || entry.seq < _from) {
    return true;
  }
  // Чужий таск - не наша відповідь. Порівнюємо рядки, а не вказівники:
  // pcTaskGetName() повертає вказівник у TCB, і покладатись на його
  // стабільність не варто.
  if (entry.task == nullptr || strcmp(entry.task, _task) != 0) {
    return true;
  }

  // Ліміт вичерпано: далі лише рахуємо втрачені рядки, щоб сказати про це в
  // маркері. Без цього команда, що зациклилась у виводі, забила б брокер.
  if (_chunksSent >= kMaxChunks) {
    ++_droppedLines;
    return true;
  }

  char line[kMaxLine];
  const size_t length = journalFormatLine(entry, line, sizeof(line));
  if (length == 0) return true;

  memcpy(_buffer + _length, line, length);
  _length += length;
  _buffer[_length++] = '\n';

  // Рвемо по межі рядка, поки в буфері ще гарантовано влазить наступний -
  // так жоден рядок не розрізається навпіл.
  if (full()) {
    flushChunk(false);
  }
  return true;
}

void CommandResponse::finish() {
  if (_finished) {
    return;
  }

  // Відписуємось ПЕРШИМ ділом: після цього виклику помпа гарантовано не
  // всередині нашого _deliver() (див. Journal::unsubscribe), тож далі можна
  // чіпати буфер без замка.
  Journal::instance().unsubscribe(_sub);
  _sub = kInvalidJournalSub;
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

  // Re-entrancy guard тут більше не потрібен: доставка логує з ЧУЖОГО таска
  // (помпа - для проміжних порцій, loop() - для фінальної), і фільтр у
  // _deliver() відкидає ці рядки сам.
  _target->deliver(_buffer, _length, isFinal);

  _length = 0;
  ++_chunksSent;
}
