#pragma once

#include <Arduino.h>

// Схема топіків EcoFlow Open Platform (developer-eu.ecoflow.com).
//
// ВАЖЛИВО: усі топіки починаються з ПРОВІДНОГО '/' - це частина схеми, а не
// друкарська помилка. Саме тому MqttConfig::useKeyGenerator для EcoFlow-клієнта
// має бути false: MqttKeyGenerator::trimSlashes() зрізав би провідний слеш і
// брокер відхилив би підписку.
//
// {account} - це certificateAccount з GET /iot-open/sign/certification
//             (він же MQTT username), НЕ accessKey.
// {sn}      - серійний номер пристрою з GET /iot-open/sign/device/list.
namespace EcoFlowMqttTopics {

// Періодична телеметрія пристрою (основне джерело даних).
inline String quota(const String &account, const String &sn) {
    return "/open/" + account + "/" + sn + "/quota";
}

// Онлайн/офлайн статус пристрою.
inline String status(const String &account, const String &sn) {
    return "/open/" + account + "/" + sn + "/status";
}

// Публікація команди зміни параметра / відповідь на неї.
inline String set(const String &account, const String &sn) {
    return "/open/" + account + "/" + sn + "/set";
}
inline String setReply(const String &account, const String &sn) {
    return "/open/" + account + "/" + sn + "/set_reply";
}

// Запит поточних значень / відповідь на нього.
inline String get(const String &account, const String &sn) {
    return "/open/" + account + "/" + sn + "/get";
}
inline String getReply(const String &account, const String &sn) {
    return "/open/" + account + "/" + sn + "/get_reply";
}

// Root-підписка одразу на всі топіки акаунта. ACL EcoFlow прив'язаний до
// certificateAccount, тому '#' у межах свого акаунта дозволений (на відміну
// від глобального "#", який брокер відхиляє).
inline String accountWildcard(const String &account) {
    return "/open/" + account + "/#";
}

}  // namespace EcoFlowMqttTopics
