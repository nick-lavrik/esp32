#include "GmailSender.h"

static const char* SMTP_HOST = "smtp.gmail.com";
static const uint16_t SMTP_PORT = 465; // SSL
// static const uint16_t SMTP_PORT = 587; // TLS

GmailSender::GmailSender(const char* senderEmail, const char* appPassword, const char* senderName)
    : _senderEmail(senderEmail), _appPassword(appPassword), _senderName(senderName) {
}

void GmailSender::begin() {
    _smtp.debug(0); // 0 = без детального логу бібліотеки, 1 - з логом
    _smtp.callback(smtpCallback);
}

void GmailSender::smtpCallback(SMTP_Status status) {
    Serial.println(status.info());
}

bool GmailSender::sendEmail(const char* recipientEmail, const char* subject, const char* message) {
    Session_Config config;
    config.server.host_name = SMTP_HOST;
    config.server.port = SMTP_PORT;
    config.login.email = _senderEmail.c_str();
    config.login.password = _appPassword.c_str();
    config.login.user_domain = "";

    SMTP_Message msg;
    msg.sender.name = _senderName;
    msg.sender.email = _senderEmail;
    msg.subject = subject;
    msg.addRecipient("Recipient", recipientEmail);
    msg.text.content = message;
    msg.text.charSet = "utf-8";
    msg.text.transfer_encoding = Content_Transfer_Encoding::enc_7bit;

    if (!_smtp.connect(&config)) {
        Serial.printf("SMTP connect failed: %s\n", _smtp.errorReason().c_str());
        return false;
    }

    if (!MailClient.sendMail(&_smtp, &msg)) {
        Serial.printf("Send failed: %s\n", _smtp.errorReason().c_str());
        return false;
    }

    return true;
}
