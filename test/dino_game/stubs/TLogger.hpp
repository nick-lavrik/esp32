#pragma once
#include <cstdio>
class TLogger {
public:
  explicit TLogger(const char* t) : _t(t) {}
  template <typename... A> void info(const char* f, A... a) const { printf("[%s] ", _t); printf(f, a...); printf("\n"); }
  template <typename... A> void error(const char* f, A... a) const { printf("[%s ERR] ", _t); printf(f, a...); printf("\n"); }
  template <typename... A> void warn(const char* f, A... a) const { printf("[%s WARN] ", _t); printf(f, a...); printf("\n"); }
  template <typename... A> void debug(const char* f, A... a) const {}
private:
  const char* _t;
};
