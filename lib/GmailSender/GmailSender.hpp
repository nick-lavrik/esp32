#pragma once

#if __has_include(<ESP_Mail_Client.h>)
#define HAS_GMAIL_SENDER 1
#include <Arduino.h>
#include <ESP_Mail_Client.h>
#include <TLogger.hpp>

// Клас-обгортка над ESP-Mail-Client для відправки email через Gmail SMTP.
// Використовується однаково в обох проектах: esp32-st7789 та esp32-4848s040.
//
// ВАЖЛИВО: Gmail більше не приймає звичайний пароль облікового запису для SMTP.
// Потрібно:
//   1) Увімкнути двофакторну автентифікацію на акаунті Google
//   2) Згенерувати "App Password" (Пароль застосунку) на myaccount.google.com/apppasswords
//   3) Використовувати саме цей 16-символьний пароль тут, а не звичайний пароль

class GmailSender {
public:
    GmailSender(const char* senderEmail, const char* appPassword, const char* senderName = "ESP32");

    // Повертає true при успішній відправці, false - при помилці (деталі в Serial)
    bool sendEmail(const char* recipientEmail, const char* subject, const char* message);

    // Викликається один раз, наприклад, у setup(), щоб підписатись на статус-колбек
    void begin();

private:
    String _senderEmail;
    String _appPassword;
    String _senderName;
    SMTPSession _smtp;

    static void smtpCallback(SMTP_Status status);

    const TLogger _logger{"gmail"};
};
#else
#define HAS_GMAIL_SENDER 0
#endif
