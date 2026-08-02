#pragma once
#include <map>
#include <string>
#include "ILogger.hpp"

// Крос-платформенний менеджер рівнів логування з підтримкою ієрархії тегів
// через крапку, напр.: "mqtt" -> "mqtt.send" -> "mqtt.send.heartbeat".
// Резолвинг рівня для тега йде від найбільш специфічного до найзагальнішого,
// якщо явний рівень не встановлений - використовується defaultLevel.
class LogLevelManager {
public:
    static LogLevelManager& instance();

    void setLevel(const char* tag, LogLevel level);
    void clearLevel(const char* tag);
    LogLevel getLevel(const char* tag) const;

    void setDefaultLevel(LogLevel level);
    LogLevel getDefaultLevel() const;

private:
    LogLevelManager() = default;

    std::map<std::string, LogLevel> _levels;
    LogLevel _defaultLevel = LogLevel::Info;
};
