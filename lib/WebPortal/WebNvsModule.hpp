#pragma once

// Розділ "nvs": перегляд і редагування записів ConfigStorage прямо з браузера.
//
// Навіщо. Половина конфігурації пристрою живе в NVS, і досі єдиним способом
// її побачити був serial-дамп ('config dump'), а змінити - окрема команда під
// кожен ключ. Коли плата висить під стелею на скотчі, це означає "зняти,
// підключити кабель, перезавантажити". Тут - той самий список, але з
// редагуванням і видаленням.
//
// Namespace'и. Писати можна ЛИШЕ в свій - той, що відкрив ConfigStorage у
// begin() (у цьому проєкті PIO_PIOENV). Чужі namespace'и того ж розділу
// (nvs.net80211 - стан WiFi-стека, phy - калібрування радіо, господарство
// бібліотек) видно тільки на читання: кнопка Delete поруч із калібруванням
// радіо - це boot-loop, а користь від правки бінарних внутрішностей без схеми
// нульова. Обмеження тримається на пристрої, а не в UI: сторінка може
// попросити що завгодно, роути запису відмовлять.
//
// Типи. NVS зберігає тип разом зі значенням, але блоб ним не описується:
// Preferences::putFloat пише саме blob, туди ж лягають масиви (setStringArray)
// і структури (setStruct). Тому blob показується як "<N bytes>" і його можна
// лише ВИДАЛИТИ - редагувати наосліп те, чиєї схеми не знаєш, гірше, ніж не
// редагувати взагалі. Створювати й правити можна рядки, цілі та булеві.
//
// Зате блоб можна ПОДИВИТИСЬ: /api/nvs/blob віддає сирі байти, сторінка малює
// з них звичний xxd-дамп. Саме заради цього випадку readBlob() в ConfigStorage
// і зроблено публічним.
//
// Про секрети. Цей розділ показує сховище як є, включно з тим, що в ньому
// лежать паролі: 'nm_conn' - це JSON профілів WiFi разом із паролями, в
// nvs.net80211 є 'ap.passwd'. Вибірково засліплювати окремі ключі сенсу немає
// - сусідній рядок тієї ж таблиці покаже те саме, - тому розділ вважається
// привілейованим цілком. Захист від чужих очей тут один і він уже є: пароль
// порталу ('web auth <user> <pass>'). Поки він не заданий, портал відкритий
// будь-кому, хто вже в мережі пристрою.
//
// Виконання. І читання, і запис - flash I/O, тому все йде через WebJobQueue,
// тобто в loop(), а не в таску сервера. Через це навіть GET списку відповідає
// номером задачі, а не даними: знімок у памʼяті (як у WebCommandsModule) тут
// не годиться - NVS пишуть десятки місць прошивки, і список застарівав би
// мовчки.

#include <Arduino.h>
#include <ESPAsyncWebServer.h>

#include <TLogger.hpp>

#include "IWebModule.hpp"

// Межа довжини значення, яке приймається з форми. NVS тримає рядки до ~4000
// байт, але значення ще й проходить через тіло POST і слот WebJobQueue, тому
// стеля нижча. 2 КБ взято не зі стелі NVS, а з найбільшого реального ключа:
// 'nm_conn' з кількома WiFi-профілями - це вже під пів кілобайта, і його
// редагують саме тут.
#ifndef WEB_NVS_MAX_VALUE
#define WEB_NVS_MAX_VALUE 2048
#endif

// Скільки байтів блоба ПОКАЗУВАТИ в hex-view. У JSON вони їдуть як hex, тобто
// вдвічі довшим рядком, і той рядок ще й лежить у слоті WebJobQueue - тому
// стеля явна, а хвіст позначається як truncated, а не мовчки зникає.
#ifndef WEB_NVS_MAX_BLOB
#define WEB_NVS_MAX_BLOB 1024
#endif

// Скільки байтів блоба можна ПРОЧИТАТИ заради цього.
//
// Чому це окреме число, більше за попереднє: nvs_get_blob() не вміє читати
// шматок. Якщо буфер менший за збережений блоб, він повертає
// ESP_ERR_NVS_INVALID_LENGTH і не пише нічого - тобто "прочитати перший
// кілобайт" неможливо в принципі, блоб береться цілим. Через це 'phy/cal_data'
// (1904 B) давав порожній дамп замість обрізаного.
//
// Більший за цю стелю блоб не читаємо взагалі: буфер живе в heap'і C6 поруч із
// самим JSON-відповіді, і мовчки з'їсти на цьому десяток кілобайт гірше, ніж
// сказати "завеликий".
#ifndef WEB_NVS_MAX_BLOB_READ
#define WEB_NVS_MAX_BLOB_READ 4096
#endif

// Скільки записів віддавати в одному списку. Свій namespace - це півтора
// десятка ключів, а от у nvs.net80211 їх бувають сотні, і цілий такий список
// у String - це кілька десятків кілобайт heap'а на C6.
#ifndef WEB_NVS_MAX_ENTRIES
#define WEB_NVS_MAX_ENTRIES 128
#endif

class ConfigStorage;

class WebNvsModule : public IWebModule {
public:
  static constexpr size_t kMaxValue = WEB_NVS_MAX_VALUE;
  static constexpr size_t kMaxBlob = WEB_NVS_MAX_BLOB;
  static constexpr size_t kMaxBlobRead = WEB_NVS_MAX_BLOB_READ;
  static constexpr size_t kMaxEntries = WEB_NVS_MAX_ENTRIES;

  explicit WebNvsModule(ConfigStorage& storage) : _storage(storage) {}

  const char* name() const override { return "nvs"; }
  void registerRoutes(AsyncWebServer& server, WebPortal& portal) override;

private:
  // Усі три виконуються з WebJobQueue, тобто в контексті loop().
  // Чи це наш namespace (порожнє ім'я = наш). Запис дозволений лише в нього.
  bool _isOwn(const String& ns) const;

  String _listJob(const String& ns);
  String _blobJob(const String& key, const String& ns);
  String _saveJob(const String& key, const String& type, const String& value);
  String _deleteJob(const String& key);

  ConfigStorage& _storage;

  const TLogger _logger{"web"};
};
