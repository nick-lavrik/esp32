#pragma once

// Обгортка над mobizt/ReadyMail для відправки email через Gmail SMTP.
//
// Чому ReadyMail, а не ESP_Mail_Client (як було до 2026-08-31): той тягнув
// ВЛАСНИЙ сокетний шар і власний BearSSL замість ядрових, і ця зв'язка не
// працює на RISC-V - на C6/C3 сесія конектилась і не читала відповіді сервера,
// хоч на Xtensa та сама версія відправляла лист нормально. Автор ESP_Mail_Client
// позначив його deprecated саме на користь ReadyMail. Головне для нас: ReadyMail
// свого стека не має - працює поверх переданого Client, тобто поверх mbedTLS
// ядра, який на C6 перевірено робочим (команда `smtp-probe 465`).
// Повний розбір - docs/architecture.md, розділ «Пошта: GmailSender».
//
// ВАЖЛИВО: Gmail не приймає звичайний пароль облікового запису для SMTP.
// Потрібно:
//   1) Увімкнути двофакторну автентифікацію на акаунті Google
//   2) Згенерувати "App Password" на myaccount.google.com/apppasswords
//   3) Використовувати саме цей 16-символьний пароль тут
//
// sendEmail() БЛОКУЄ таск, з якого викликаний. Якщо це головний таск - на весь
// час SMTP-сесії стає все: рендер кадру, MQTT, тач. Таймаути ReadyMail
// (конект 3 с, читання 120 с) лежать у приватному smtp_options і публічного
// сеттера не мають, тому найгірший випадок - хвилина-дві мертвої плати на
// зависшому сервері. Прибрати блокування - це виносити SMTP в окремий таск,
// як EcoflowClient робить з REST.

// Хост і порт підставляються з secrets.ini (gmail_smtp / gmail_port) через
// build_flags; дефолти - щоб бібліотека збиралась і без них.
#ifndef GMAIL_SMTP_HOST
#define GMAIL_SMTP_HOST "smtp.gmail.com"
#endif

// 465 - implicit SSL (TLS одразу після конекту).
// 587 - STARTTLS: конект відкритий, апгрейд робить callback через
//       NetworkClientSecure::startTLS(). Це ESP32-only: BearSSL-клієнт
//       ESP8266 апгрейду plain->TLS не вміє (див. tlsHandshake()).
#ifndef GMAIL_SMTP_PORT
#define GMAIL_SMTP_PORT 465
#endif

// Лишений як довідкове число для попереджень і для setTimeout() до конекту:
// ReadyMail перезаписує таймаут клієнта своїм на початку сесії.
#ifndef GMAIL_SMTP_TIMEOUT_SEC
#define GMAIL_SMTP_TIMEOUT_SEC 10
#endif

// 1 - детальний лог самої ReadyMail у Serial (стадії конекту, обмін з сервером).
#ifndef GMAIL_SMTP_DEBUG
#define GMAIL_SMTP_DEBUG 0
#endif

#if __has_include(<ReadyMail.h>)
#define HAS_GMAIL_SENDER 1

#include <Arduino.h>
#include <WiFiClientSecure.h>

#include <TLogger.hpp>

// Ці три прапорці ReadyMail читає ПРИ include, а не при виклику, тому стоять
// саме тут і саме перед ним:
//   ENABLE_SMTP          - без нього SMTP-класів у бібліотеці просто немає;
//   ENABLE_DEBUG         - вмикає її власний лог;
//   READYMAIL_TIME_SOURCE - звідки брати час для заголовка Date:.
#define ENABLE_SMTP
#if GMAIL_SMTP_DEBUG
#define ENABLE_DEBUG
#define READYMAIL_DEBUG_PORT Serial
#endif
#define READYMAIL_TIME_SOURCE time(nullptr)
#include <ReadyMail.h>

class GmailSender {
public:
  GmailSender(const char* senderEmail, const char* appPassword, const char* senderName = "ESP32");

  // Повертає true при успішній відправці, false - при помилці (деталі в лог).
  bool sendEmail(const char* recipientEmail, const char* subject, const char* message);

  // Одноразова ініціалізація; sendEmail() кличе її сам.
  void begin();

private:
  // true - порт із implicit SSL, false - відкритий конект з апгрейдом STARTTLS.
  static constexpr bool kImplicitSsl = (GMAIL_SMTP_PORT != 587);

  // Колбеки ReadyMail - прості вказівники на функції, без capture, тому
  // дотягуватись до об'єкта доводиться через статичний вказівник. Екземпляр
  // у проєкті один (mailer у src/main.cpp).
  static GmailSender* _active;

  static void smtpCallback(SMTPStatus status);
  static void tlsHandshake(bool& success);

  // Пампить ReadyMail, поки done() не стане true або не вийде дедлайн.
  //
  // Навіщо власний цикл: усі три кроки (connect/authenticate/send) мають
  // блокуючий режим за замовчуванням, але його внутрішній таймаут читання -
  // 120 с, лежить у приватному smtp_options і сеттера не має. Тобто на
  // зависшому сервері блокуючий режим - це дві хвилини мертвої плати. З
  // await=false цикл наш, і дедлайн - GMAIL_SMTP_TIMEOUT_SEC.
  //
  // Заміряно на C6: блокуючий режим ще й просто НЕ працює - вітання сервера
  // так і не прочитується (readTimeout після 120 с), тоді як з цим циклом
  // усе проходить за секунду. Тому await=false тут не оптимізація.
  template <typename Predicate>
  bool waitUntil(Predicate done, const char* what) {
    const uint32_t deadline = millis() + GMAIL_SMTP_TIMEOUT_SEC * 1000UL;
    while (!done() && millis() < deadline) {
      _smtp.loop();
      delay(10);
    }
    if (!done()) {
      _logger.error("timed out waiting for %s (%u s)", what, (unsigned)GMAIL_SMTP_TIMEOUT_SEC);
      return false;
    }
    return true;
  }

  // Закриває сесію й логує heap. Викликається на ВСІХ виходах sendEmail().
  void closeIdleSession();

  String _senderEmail;
  String _appPassword;
  String _senderName;

  // Порядок оголошення важливий: _smtp у конструкторі бере посилання на
  // _sslClient, тож клієнт мусить бути створений раніше.
  //
  // При implicit SSL callback мусить бути nullptr, і це не косметика: якщо він
  // заданий, ReadyMail викликає його ще в initial_state і переводить автомат у
  // smtp_state_start_tls_ack - чекати 220 на команду STARTTLS, якої ніхто не
  // надсилав. Симптом - "SMTP connect failed" одразу після "TLS handshake done"
  // (SMTPConnection.h, tlsHandshake()). З nullptr автомат натомість читає
  // вітання сервера й шле EHLO, як і має бути на 465.
  WiFiClientSecure _sslClient;
  SMTPClient _smtp{_sslClient, kImplicitSsl ? static_cast<TLSHandshakeCallback>(nullptr) : &GmailSender::tlsHandshake,
                   !kImplicitSsl};

  const TLogger _logger{"gmail"};
};
#else
#define HAS_GMAIL_SENDER 0
#endif
