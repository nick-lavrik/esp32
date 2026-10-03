#include "MailCommands.hpp"

#if HAS_GMAIL_SENDER

#include <WiFiClientSecure.h>

#include <CommandQueue.hpp>
#include <EmailTarget.hpp>
#include <Logger.hpp>
#include <TLogger.hpp>
#include <memory>

#include "App/AppGlobals.hpp"

#if defined(ESP8266)
#include <ESP8266WiFi.h>
#else
#include <WiFi.h>
#endif

namespace {
// SMTP-дим-тест: найкоротший шлях перевірити, що лист узагалі виходить із
// плати. Тіло листа - фіксований рядок, тому це перевірка саме транспорту, а
// не механізму захоплення виводу (для нього є команда "mailto").
//
// Раніше тут був власний виклик mailer.sendEmail() - тобто БЕЗ паузи MQTT,
// обов'язкової на C6, де дві TLS-сесії не влазять у heap. Тепер відправку
// робить той самий EmailTarget, що й "mailto": suspend/resume і логування
// heap лежать в одному місці, а не в двох копіях, що розходяться.
void sendEmail() {
  static TLogger logger("sendmail");

  if (!WiFi.isConnected()) {
    logger.error("wifi is not connected");
    return;
  }

  // SMTP над TLS вимагає валідного часу для перевірки сертифіката. Без цієї
  // перевірки бібліотека сама полізе по NTP і заблокує таск ще на 10 с.
  if (!ntp.isSynced()) {
    logger.error("NTP not ready - valid system time required for TLS");
    return;
  }

  // Попередження ДО відправки, а не після: сесія блокує цей таск, тобто на
  // цей час стає і рендер кадру, і MQTT, і тач. Без цього рядка плата
  // виглядає завислою (саме так це й читалось у консолі).
  logger.warn("sending test mail to %s - blocks this task until the SMTP session ends", GMAIL_TEST_RECIPIENT);

  EmailTarget target(mailer, GMAIL_TEST_RECIPIENT, PIO_PIOENV ": smtp smoke test",
#if HAS_MQTT_CLIENT
                     &mqtt
#else
                     nullptr
#endif
  );
  static const char kBody[] = "SMTP smoke test from " PIO_PIOENV ".\n";
  target.deliver(kBody, sizeof(kBody) - 1, /*isFinal=*/true);
}
}  // namespace

void registerMailCommands(SerialCommander& commander) {
  // command: mailto
  //
  // Той самий механізм відповіді, що для MQTT-команд, але з іншим приймачем -
  // вивід вкладеної команди їде листом. Тут це ще й єдиний користувач
  // EmailTarget; для команд за розкладом cron-лямбда так само захоплює
  // shared_ptr на приймач і віддає результат при кожному спрацюванні.
  commander.registerCommand(
      "mailto", "run a command and send its output by email: mailto <address> <command>", [](const String& args) {
        static TLogger logger("mailto");

        const int spaceIdx = args.indexOf(' ');
        if (spaceIdx < 0) {
          logger.info("use: mailto <address> <command>");
          return;
        }

        const String address = args.substring(0, spaceIdx);
        String command = args.substring(spaceIdx + 1);
        command.trim();
        if (address.length() == 0 || command.length() == 0) {
          logger.info("use: mailto <address> <command>");
          return;
        }

        auto target = std::make_shared<EmailTarget>(mailer, address, String(PIO_PIOENV ": ") + command,
#if HAS_MQTT_CLIENT
                                                    &mqtt
#else
                                                    nullptr
#endif
        );
        // ПРЯМО ЗАРАЗ, повз чергу: вкладена команда мусить виконатись у цьому
        // ж виклику, інакше лист пішов би без її виводу.
        commandQueue.runNow(command.c_str(), std::move(target));
      });
  commander.registerCommand("sendmail", "send an SMTP smoke-test email to " GMAIL_TEST_RECIPIENT,
                            [](const String& args) { sendEmail(); });

  // command: smtp-probe
  //
  // Конект до SMTP-хоста без поштової бібліотеки. Потрібен, щоб відділити
  // "мережа/сервер недосяжні" від "бібліотека не читає відповідь": обидва
  // випадки в логах поштової бібліотеки виглядають однаково. Той самий підхід,
  // що `sdbb` для SD - перевірка найнижчого шару своїми руками. Саме ця
  // команда й довела, що на C6 винна була бібліотека, а не плата.
  commander.registerCommand(
      "smtp-probe", "probe the SMTP host without the mail library: smtp-probe [port] (465 = TLS)",
      [](const String& args) {
        static TLogger logger("smtp");

        if (!WiFi.isConnected()) {
          logger.error("wifi is not connected");
          return;
        }

        // Порт з аргументу, інакше зібраний. 465 пробуємо через TLS ядра
        // (WiFiClientSecure, mbedTLS) - це рівно той шар, на який спирається
        // будь-яка поштова бібліотека без власного стека, тож проба каже, чи
        // взагалі можлива TLS-сесія з Gmail на цій платі.
        String portArg = args;
        portArg.trim();
        const uint16_t port = portArg.length() > 0 ? (uint16_t)portArg.toInt() : (uint16_t)GMAIL_SMTP_PORT;
        const bool useTls = (port == 465);

#if defined(ESP32)
        WiFiClientSecure tlsClient;
        if (useTls) {
          // Сертифікат навмисно не перевіряємо: тут перевіряється сама
          // можливість handshake, а не довіра до сервера.
          tlsClient.setInsecure();
        }
#endif
        WiFiClient plainClient;
#if defined(ESP32)
        Client& client = useTls ? static_cast<Client&>(tlsClient) : static_cast<Client&>(plainClient);
#else
        Client& client = plainClient;
        if (useTls) {
          logger.warn("TLS probe is ESP32-only, falling back to plain TCP");
        }
#endif

        logger.info("probing %s:%u (%s), %u B free, largest block %u B", GMAIL_SMTP_HOST, (unsigned)port,
                    useTls ? "TLS" : "plain", (unsigned)ESP.getFreeHeap(),
#if defined(ESP32)
                    (unsigned)ESP.getMaxAllocHeap()
#else
                    (unsigned)ESP.getMaxFreeBlockSize()
#endif
        );

        const uint32_t started = millis();
        if (!client.connect(GMAIL_SMTP_HOST, port)) {
          logger.error("connect to %s:%u failed after %lu ms", GMAIL_SMTP_HOST, (unsigned)port,
                       (unsigned long)(millis() - started));
          return;
        }
        logger.info("connected to %s:%u in %lu ms, waiting for greeting", GMAIL_SMTP_HOST, (unsigned)port,
                    (unsigned long)(millis() - started));

        // Банер (код 220) сервер шле сам, одразу після конекту: на 587 у
        // відкритому вигляді, на 465 - уже всередині TLS-сесії.
        String greeting;
        const uint32_t deadline = millis() + 5000;
        while (millis() < deadline && !greeting.endsWith("\n")) {
          while (client.available() > 0) {
            greeting += (char)client.read();
          }
          delay(10);
        }

        if (greeting.length() == 0) {
          logger.error("connected but silent for 5000 ms - no greeting");
          if (!useTls && port == 465) {
            logger.info("port 465 is implicit TLS - silence over plain TCP is expected here");
          }
        } else {
          greeting.trim();
          logger.info("greeting: %s", greeting.c_str());
        }
        client.stop();
      });
}

#endif  // HAS_GMAIL_SENDER
