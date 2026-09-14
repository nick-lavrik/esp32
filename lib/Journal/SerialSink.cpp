#include "SerialSink.hpp"

#include <Arduino.h>
#include <RwLock.hpp>

#include <cstring>

// Скільки чекати на зайнятий Serial. Те саме значення, що було в
// SerialLogger::log() до рефактора.
#ifndef SERIAL_SINK_TIMEOUT_MS
#define SERIAL_SINK_TIMEOUT_MS 10
#endif

bool SerialSink::deliver(const JournalEntry& entry) {
  // Префікс + текст + '\n' в один буфер і один print(): рядок має потрапити в
  // UART цілим шматком, інакше його розріже чужий вивід.
  char line[JournalEntry::kTextSize + 16];

  // -1: лишаємо байт під '\n', який дописуємо самі (в MQTT його, навпаки, нема).
  const size_t length = journalFormatLine(entry, line, sizeof(line) - 1);
  if (length == 0) return true;  // не влазить навіть префікс - нічого друкувати
  line[length] = '\n';

  const size_t total = length + 1;  // з '\n', без '\0'

  // ПЕРЕВІРКА МІСЦЯ В TX ОБОВ'ЯЗКОВА, і це не оптимізація.
  //
  // На платах з native USB CDC стоїть Serial.setTxTimeoutMs(0) (див.
  // src/setup.h): без нього print() може зависнути назавжди, якщо хост не
  // вичитує буфер. Ціна - print() при повному буфері не чекає, а МОВЧКИ
  // відкидає зайві байти й нічого про це не каже. Тобто "записав" і "виїхало"
  // - різні речі.
  //
  // Саме на цьому попався перший варіант приймача: він повертав true за фактом
  // узяття замка, журнал вважав рядок доставленим і зсував курсор, а рядок
  // насправді зникав. Команда 'list' через це видавала різні підмножини команд
  // від прогону до прогону.
  //
  // Тому: пишемо, лише якщо влазить ЦІЛИЙ рядок. Не влазить - повертаємо
  // false, курсор лишається на місці, і помпа принесе цей самий запис ще раз.
  bool written = false;
  rwlock::write(Serial, SERIAL_SINK_TIMEOUT_MS, [&line, total, &written]() {
    if (static_cast<size_t>(Serial.availableForWrite()) < total) return;
    Serial.write(reinterpret_cast<const uint8_t*>(line), total);
    written = true;
  });
  return written;
}

// Скільки максимум чекати, поки хост вичитає TX. Довше - лише якщо консоль
// відкрита й читає; якщо ніхто не читає, дамп однаково нікуди не приїде, і
// вішати пристрій назавжди не можна.
#ifndef SERIAL_RAW_TIMEOUT_MS
#define SERIAL_RAW_TIMEOUT_MS 2000
#endif

// Скільки чекати на сам замок. Більше, ніж у приймача: він пише один короткий
// рядок і відпускає, а ми не маємо права загубити шматок дампу.
#ifndef SERIAL_RAW_LOCK_MS
#define SERIAL_RAW_LOCK_MS 1000
#endif

bool SerialSink::writeRaw(const char* text, size_t length) {
  if (text == nullptr) return false;
  if (length == 0) length = strlen(text);

  // Три вимоги одночасно, і кожна з'явилась після конкретної поломки на залізі:
  //
  // 1. ЗАМОК НА ВЕСЬ ВИКЛИК. Інакше в щілину між шматками влазить приймач
  //    журналу: "0x2103,0[I][net] connecting to 'STARLINK'..." посеред масиву
  //    пікселів.
  // 2. ЧЕКАТИ МІСЦЯ ПЕРЕД ПОЧАТКОМ, а не посеред рядка - щоб таймаут не лишив
  //    у потоці обірваний шматок.
  // 3. ДОПИСУВАТИ ДО КІНЦЯ. Serial.write() на USB CDC повертає МЕНШЕ, ніж
  //    обіцяв availableForWrite(): другий прогін 'bg-save' дав
  //    "0x2103,0x2103,003,0x2103" - дві літери просто зникли всередині одного
  //    виклику write(). Тому дивимось на повернене значення, а не на віру.
  bool complete = false;
  rwlock::write(Serial, SERIAL_RAW_LOCK_MS, [&text, &length, &complete]() {
    uint32_t deadline = millis() + SERIAL_RAW_TIMEOUT_MS;

    // Спершу - місце під ЦІЛИЙ шматок.
    while (static_cast<size_t>(Serial.availableForWrite()) < length) {
      if (static_cast<int32_t>(millis() - deadline) >= 0) return;  // хост не читає
      delay(1);
    }

    size_t sent = 0;
    while (sent < length) {
      const size_t written = Serial.write(reinterpret_cast<const uint8_t*>(text) + sent, length - sent);
      if (written > 0) {
        sent += written;
        deadline = millis() + SERIAL_RAW_TIMEOUT_MS;  // є прогрес - таймер наново
        continue;
      }
      if (static_cast<int32_t>(millis() - deadline) >= 0) return;  // обірвались, скажемо про це
      delay(1);
    }
    complete = true;
  });
  return complete;
}

void SerialSink::attach(Journal& journal) {
  // Патерн "" і рівень Verbose: serial бере все, що пройшло фільтр логера.
  // Саме він лишається повним потоком, з яким звіряються решта приймачів.
  //
  // lossless=true - єдиний такий приймач у проєкті. Консоль не має права
  // губити рядки: команда 'list' або 'sdmap' вивалює їх сотнями швидше, ніж
  // UART фізично встигає, і без протитечії вивід виходив рваним (перевірено:
  // два прогони 'list' давали різні підмножини команд).
  journal.subscribe("serial", "", LogLevel::Verbose, &SerialSink::deliver, /*lossless=*/true);
}
