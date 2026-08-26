#pragma once

#include <Arduino.h>

#include "EcoflowMqttCredentials.hpp"

// Автентифікація в ПРИВАТНОМУ API застосунку EcoFlow (api.ecoflow.com), а не в
// Open Platform.
//
// НАВІЩО, якщо Open Platform уже працює: він віддає обрізаний набір полів для
// частини моделей. DELTA mini через /open/.../quota шле 23 поля, де ВСІ ватти
// нулі (єдине поле про вхід - bmsMaster.inputWatts, і воно завжди 0), тому
// наявність мережі доводилось вгадувати з динаміки заряду. Той самий пристрій
// через приватний канал віддає 201 поле, зокрема inv.acInVol=211658,
// inv.acInFreq=50 та inv.inputWatts=45 - тобто рівно те, що показує застосунок.
//
// Ціна: API неофіційний, EcoFlow може змінити його без попередження. Тому
// Open Platform лишається як fallback (див. EcoflowClient::Channel).
//
// Схема (перевірено на живому акаунті):
//   1. POST /auth/login  {email, password: base64, scene: IOT_APP, ...} -> JWT
//   2. GET  /iot-auth/app/certification  (Bearer <JWT>) -> MQTT-креденшели
//
// JWT живе 30 днів, але потрібен лише щоб ОТРИМАТИ MQTT-креденшели; самі
// креденшели стабільні між викликами, тому їх достатньо випустити раз і
// зберегти (див. команду 'ecoflow-login').
class EcoflowAppAuthClient {
public:
    EcoflowAppAuthClient(const String &email, const String &password);

    // Виконує логін і одразу забирає MQTT-креденшели. БЛОКУЮЧИЙ: два HTTPS-
    // запити, тобто викликати лише з rest-таска (стек!) і при паузі MQTT.
    bool fetchMqttCredentials(EcoflowMqttCredentials &outCredentials);

    // Ідентифікатор користувача з логіну - потрібен для clientId, який брокер
    // очікує у форматі "ANDROID_<uuid>_<userId>".
    const String &userId() const { return _userId; }
    const String &lastError() const { return _lastError; }

private:
    String _email;
    String _password;
    String _userId;
    String _lastError;

    static const char *kApiHost;  // "api.ecoflow.com"

    bool login(String &outToken);
    bool certification(const String &token, EcoflowMqttCredentials &outCredentials);
    // Пароль передається у base64 - вимога цього API.
    static String base64Encode(const String &value);
};
