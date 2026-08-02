#include "LogLevelManager.hpp"

LogLevelManager& LogLevelManager::instance() {
    static LogLevelManager mgr;
    return mgr;
}

void LogLevelManager::setLevel(const char* tag, LogLevel level) {
    _levels[tag] = level;
}

void LogLevelManager::clearLevel(const char* tag) {
    _levels.erase(tag);
}

void LogLevelManager::setDefaultLevel(LogLevel level) {
    _defaultLevel = level;
}

LogLevel LogLevelManager::getDefaultLevel() const {
    return _defaultLevel;
}

LogLevel LogLevelManager::getLevel(const char* tag) const {
    std::string key(tag);

    while (true) {
        auto it = _levels.find(key);
        if (it != _levels.end()) {
            return it->second;
        }

        auto dot = key.find_last_of('.');
        if (dot == std::string::npos) {
            break;
        }
        key = key.substr(0, dot);
    }

    return _defaultLevel;
}
