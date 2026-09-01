#include "LogMirror.hpp"

std::atomic<Print*> LogMirror::_sink{nullptr};

void LogMirror::set(Print* sink) { _sink.store(sink, std::memory_order_release); }

Print* LogMirror::current() { return _sink.load(std::memory_order_acquire); }
