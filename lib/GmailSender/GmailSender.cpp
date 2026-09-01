#include "GmailSender.hpp"

#if HAS_GMAIL_SENDER

namespace {
// ESP8266 не має getMaxAllocHeap() - там найбільший вільний блок звуть
// getMaxFreeBlockSize(). Той самий #if, що в команді "heap" у src/main.cpp.
inline unsigned largestFreeBlock() {
#if defined(ESP32)
  return (unsigned)ESP.getMaxAllocHeap();
#else
  return (unsigned)ESP.getMaxFreeBlockSize();
#endif
}
}  // namespace

GmailSender* GmailSender::_active = nullptr;

GmailSender::GmailSender(const char* senderEmail, const char* appPassword, const char* senderName)
    : _senderEmail(senderEmail), _appPassword(appPassword), _senderName(senderName) {}

void GmailSender::begin() {
  static bool once = false;
  if (once) return;
  once = true;

  _active = this;

  // Сертифікат навмисно не перевіряємо: CA в прошивці немає, а вантажити його
  // заради дим-тесту немає сенсу. Захист від підміни сервера тут відсутній -
  // це свідомий компроміс, і саме тому паролем має бути App Password, а не
  // пароль акаунта.
  _sslClient.setInsecure();
}

// "%s" обов'язково: status.text - це текст ВІД SMTP-СЕРВЕРА. Як format-рядок
// будь-який '%' у відповіді сервера змусив би vsnprintf() читати неіснуючі
// varargs зі стека.
void GmailSender::smtpCallback(SMTPStatus status) {
  if (_active == nullptr) {
    return;
  }
  if (status.errorCode != 0) {
    _active->_logger.error("[%d] %s", status.errorCode, status.text.c_str());
  } else {
    _active->_logger.debug("%s", status.text.c_str());
  }
}

// STARTTLS-апгрейд для порту 587. ReadyMail шле сам "STARTTLS", а сам
// handshake делегує сюди - бо вона не знає, який клієнт їй передали.
void GmailSender::tlsHandshake(bool& success) {
#if defined(ESP32)
  success = (_active != nullptr) && (_active->_sslClient.startTLS() == 1);
#else
  // BearSSL-клієнт ESP8266 не вміє апгрейду plain->TLS: там лишається 465.
  success = false;
#endif
}

bool GmailSender::sendEmail(const char* recipientEmail, const char* subject, const char* message) {
  begin();

#if !defined(ESP32)
  if (!kImplicitSsl) {
    _logger.error("port %u needs STARTTLS, unsupported here - use 465", (unsigned)GMAIL_SMTP_PORT);
    return false;
  }
#endif

  // Heap логуємо перед конектом навмисно: TLS-сесія просить суцільний блок, і
  // "connect failed" через його брак виглядає точно так само, як через
  // закритий порт або протухлий App Password.
  _logger.info("connecting to %s:%u (%s), %u B free, largest block %u B", GMAIL_SMTP_HOST,
               (unsigned)GMAIL_SMTP_PORT, kImplicitSsl ? "implicit SSL" : "STARTTLS",
               (unsigned)ESP.getFreeHeap(), largestFreeBlock());

#if defined(ESP32)
  if (!kImplicitSsl) {
    // Конект має початись у відкритому вигляді, апгрейд зробить tlsHandshake().
    _sslClient.setPlainStart();
  }
#endif

  if (!_smtp.connect(GMAIL_SMTP_HOST, GMAIL_SMTP_PORT, smtpCallback, kImplicitSsl, /*await=*/false) ||
      !waitUntil([this]() { return _smtp.isConnected(); }, "server greeting")) {
    _logger.error("SMTP connect failed");
    closeIdleSession();
    return false;
  }

  if (!_smtp.authenticate(_senderEmail.c_str(), _appPassword.c_str(), readymail_auth_password, /*await=*/false) ||
      !waitUntil([this]() { return _smtp.isAuthenticated(); }, "authentication")) {
    _logger.error("SMTP auth failed for %s - check the App Password", _senderEmail.c_str());
    closeIdleSession();
    return false;
  }

  // Повідомлення беремо з самої ReadyMail, а не створюємо локальне: в
  // асинхронному режимі вона тримає на нього посилання між викликами loop(),
  // і локальний об'єкт помер би одразу після виходу з цієї функції. Сама
  // бібліотека це й перевіряє - локальний SMTPMessage дає помилку -109
  // (SMTP_ERROR_UNINITIALIZE_LOCAL_SMTP_MESSAGE).
  SMTPMessage& msg = _smtp.getMessage();
  msg.headers.add(rfc822_subject, subject);
  msg.headers.add(rfc822_from, _senderName + " <" + _senderEmail + ">");
  msg.headers.add(rfc822_to, String(recipientEmail));
  msg.text.body(String(message));
  // Без явного штампу ReadyMail підставить час збірки бібліотеки, і лист
  // приїде з датою 2025 року. time() тут валідний: виклики sendmail/mailto
  // самі перевіряють ntp.isSynced() перед відправкою.
  msg.timestamp = time(nullptr);

  if (!_smtp.send(msg, /*notify=*/"", /*await=*/false) ||
      !waitUntil([this]() { return _smtp.status().isComplete; }, "message delivery")) {
    _logger.error("send to %s failed", recipientEmail);
    closeIdleSession();
    return false;
  }

  const bool ok = (_smtp.status().errorCode == 0);
  if (ok) {
    _logger.info("sent to %s", recipientEmail);
  } else {
    _logger.error("send to %s rejected by server", recipientEmail);
  }

  closeIdleSession();
  return ok;
}

// Сесію закриваємо самі: буфери mbedTLS - десятки кілобайтів, а _smtp і
// _sslClient живуть разом з об'єктом. Без цього heap лишався просілим і
// наступна TLS-операція (напр. EcoFlow REST) падала б через брак пам'яті.
void GmailSender::closeIdleSession() {
  if (_smtp.isConnected()) {
    // QUIT теж асинхронно: у блокуючому logout() сидить той самий
    // 120-секундний таймаут читання, і плата замовкала вже ПІСЛЯ успішної
    // відправки (заміряно: 45 с тишини після "sent to ...").
    _smtp.logout(/*await=*/false);
    // Дедлайн короткий і без помилки в лог: лист на цей момент уже прийнятий
    // сервером, і невідправлений QUIT нічого не псує.
    const uint32_t deadline = millis() + 2000;
    while (_smtp.isConnected() && millis() < deadline) {
      _smtp.loop();
      delay(10);
    }
  }
  _smtp.stop();
  _logger.debug("session closed, %u B free (largest block %u B)", (unsigned)ESP.getFreeHeap(),
                largestFreeBlock());
}
#endif
