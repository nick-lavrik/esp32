#pragma once

// Збирає вивід однієї команди й віддає його в ResponseTarget порціями.
//
// ПІДПИСНИК ЖУРНАЛУ З ФІЛЬТРОМ ПО ТАСКУ. Хендлери команд про це нічого не
// знають: увесь їхній вивід іде через TLogger, а сюди потрапляє все, що
// залоговано з таска, де виконується команда. Тому у відповідь заходить і те,
// що логують бібліотеки всередині команди (MqttClient, SDCardInspector,
// EspPartitionInspector) - те, чого хендлер не контролює.
//
// Фільтр саме по таску, а не глобальний: паралельно логують мережевий таск
// MqttClient ("mqtt-net") і "ecoflow-rest", і їхні рядки не мають потрапляти у
// відповідь на чужу команду.
//
// Використання (див. runCommandWithResponse() в src/main.cpp):
//   CommandResponse resp(target);
//   resp.attach();                 // підписка; далі все з ЦЬОГО таска - наше
//   commandHandler.execute(line);
//   resp.finish();                 // відписка + фінальна порція
//
// ЩО ЗМІНИЛОСЬ НА ЕТАПІ 5 (docs/journal_plan.md). Раніше це був Print-приймач,
// у який SerialLogger::log() дублював уже сформований рядок синхронно, через
// LogCaptureRegistry (реєстр "куди дублювати" з прив'язкою до таска) і
// ScopedLogCapture (RAII поверх нього). Обидва видалені, і разом з ними -
// re-entrancy guard `ScopedLogCapture{nullptr}` усередині доставки: він був
// потрібен, бо доставка сама логує (MqttClient пише warn при переповненні
// черги), і ці рядки поверталися б у буфер, який вони ж доставляють. Тепер
// доставка йде в таску помпи, її власні рядки мають чужий task - і фільтр
// відкидає їх сам. Рекурсії немає за побудовою.
//
// ВКЛАДЕНІ ВІДПОВІДІ ('mailto' усередині MQTT-команди) теж стали чесніші.
// Раніше внутрішній скоуп ПІДМІНЯВ зовнішній, і зовнішня відповідь просто не
// бачила виводу вкладеної команди. Тепер обидва приймачі підписані одночасно й
// обидва отримують ті самі рядки: зовнішній бачить надмножину, як і має бути.
//
// Підписка lossless: продюсер (таск команди) пригальмує, якщо ми не встигаємо
// розгрібати. Інакше команда на 70 рядків витіснила б власний початок із
// кільця на 32 записи - тобто відповідь мовчки втрачала б голову виводу.
//
// Розмір порції і ліміт - через build_flags:
//   -D COMMAND_RESPONSE_CHUNK_BYTES=512   (скільки байтів накопичувати)
//   -D COMMAND_RESPONSE_MAX_CHUNKS=8      (скільки порцій максимум на команду)
// Ліміт - захист від команди, що зациклилась у виводі: без нього вона забила б
// брокер. Понад ліміт рядки відкидаються з підрахунком, а фінальна порція
// містить маркер "...(truncated, N more lines)".

#include <Journal.hpp>

#include <cstddef>
#include <memory>

#include "ResponseTarget.hpp"

#ifndef COMMAND_RESPONSE_CHUNK_BYTES
#define COMMAND_RESPONSE_CHUNK_BYTES 512
#endif

#ifndef COMMAND_RESPONSE_MAX_CHUNKS
#define COMMAND_RESPONSE_MAX_CHUNKS 8
#endif

class CommandResponse {
public:
  static constexpr size_t kChunkBytes = COMMAND_RESPONSE_CHUNK_BYTES;
  static constexpr size_t kMaxChunks = COMMAND_RESPONSE_MAX_CHUNKS;

  explicit CommandResponse(std::shared_ptr<ResponseTarget> target);

  // Підписується в журнал на записи ПОТОЧНОГО таска. Кликати з того таска, де
  // далі виконуватиметься команда.
  bool attach();

  // Відписується і віддає залишок буфера з isFinal = true. Ідемпотентний:
  // повторний виклик (напр. з деструктора після явного finish()) нічого не
  // робить.
  void finish();

  ~CommandResponse();

  CommandResponse(const CommandResponse&) = delete;
  CommandResponse& operator=(const CommandResponse&) = delete;

private:
  // Приймач журналу. Завжди true: якщо ліміт вичерпано, рядок не втрачається
  // для інших приймачів - ми просто рахуємо його як відкинутий.
  bool _deliver(const JournalEntry& entry);

  // Віддає накопичене в target. Порожню порцію шле лише як фінальну.
  void flushChunk(bool isFinal);

  // true, якщо в буфері вже немає місця під ще один рядок логу.
  bool full() const { return _length + kMaxLine >= kChunkBytes; }

  // Найдовший рядок, який може прийти з журналу: текст плюс префікс.
  static constexpr size_t kMaxLine = JournalEntry::kTextSize + 16;
  static_assert(kChunkBytes > kMaxLine,
                "COMMAND_RESPONSE_CHUNK_BYTES must exceed the longest log line");

  std::shared_ptr<ResponseTarget> _target;

  JournalSubId _sub = kInvalidJournalSub;
  const char* _task = "";
  uint32_t _from = 0;  // seq, з якого починається наша відповідь

  char _buffer[kChunkBytes] = {};
  size_t _length = 0;
  size_t _chunksSent = 0;
  size_t _droppedLines = 0;
  bool _finished = false;
};
