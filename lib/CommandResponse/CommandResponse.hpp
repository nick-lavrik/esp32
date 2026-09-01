#pragma once

// Print-приймач для ScopedLogCapture: накопичує рядки логу, зібрані під час
// виконання команди, і віддає їх у ResponseTarget порціями.
//
// Використання (див. runCommandWithResponse() в src/main.cpp):
//   CommandResponse resp(target);            // target - shared_ptr<ResponseTarget>
//   {
//     ScopedLogCapture capture(resp);
//     commandHandler.execute(line);
//   }
//   resp.finish();
//
// finish() навмисно ЗА МЕЖАМИ скоупу: доставка сама логує (MqttClient пише
// warn при переповненні черги), і всередині скоупу ці рядки потрапляли б у
// відповідь, яку вони ж і доставляють. Всередині deliver() стоїть ще й
// re-entrancy guard на випадок, коли finish() усе-таки викликали в скоупі.
//
// Розмір порції і ліміт - через build_flags:
//   -D COMMAND_RESPONSE_CHUNK_BYTES=512   (скільки байтів накопичувати)
//   -D COMMAND_RESPONSE_MAX_CHUNKS=8      (скільки порцій максимум на команду)
// Ліміт - захист від команди, що зациклилась у виводі: без нього вона забила б
// брокер. Понад ліміт рядки відкидаються з підрахунком, а фінальна порція
// містить маркер "...(truncated, N more lines)".

#include <Print.h>

#include <cstddef>
#include <memory>

#include "ResponseTarget.hpp"

#ifndef COMMAND_RESPONSE_CHUNK_BYTES
#define COMMAND_RESPONSE_CHUNK_BYTES 512
#endif

#ifndef COMMAND_RESPONSE_MAX_CHUNKS
#define COMMAND_RESPONSE_MAX_CHUNKS 8
#endif

class CommandResponse : public Print {
public:
  static constexpr size_t kChunkBytes = COMMAND_RESPONSE_CHUNK_BYTES;
  static constexpr size_t kMaxChunks = COMMAND_RESPONSE_MAX_CHUNKS;

  explicit CommandResponse(std::shared_ptr<ResponseTarget> target);

  // Print: приймає готові рядки від SerialLogger (кожен уже з '\n').
  size_t write(uint8_t c) override;
  size_t write(const uint8_t* buffer, size_t size) override;

  // Віддає залишок буфера з isFinal = true. Ідемпотентний: повторний виклик
  // (напр. з деструктора після явного finish()) нічого не робить.
  void finish();

  // Без override: на ESP8266 Print не має віртуального деструктора. Об'єкт
  // завжди живе на стеку (див. runCommandWithResponse) і ніколи не видаляється
  // через Print*, тож це безпечно.
  ~CommandResponse();

  CommandResponse(const CommandResponse&) = delete;
  CommandResponse& operator=(const CommandResponse&) = delete;

private:
  // Віддає накопичене в target. Порожню порцію шле лише як фінальну.
  void flushChunk(bool isFinal);

  // true, якщо в буфері вже немає місця під ще один рядок логу.
  bool full() const { return _length + PrintQueueLineSize >= kChunkBytes; }

  // Максимальна довжина одного рядка логу (PrintQueue::kLineSize). Дублюємо
  // константу, а не include-имо PrintQueue.hpp: CommandResponse не має знати
  // про внутрішню кухню логера, а static_assert нижче ловить розбіжність.
  static constexpr size_t PrintQueueLineSize = 160;
  static_assert(kChunkBytes > PrintQueueLineSize,
                "COMMAND_RESPONSE_CHUNK_BYTES must exceed the longest log line");

  std::shared_ptr<ResponseTarget> _target;

  char _buffer[kChunkBytes] = {};
  size_t _length = 0;
  size_t _chunksSent = 0;
  size_t _droppedLines = 0;
  bool _finished = false;
};
