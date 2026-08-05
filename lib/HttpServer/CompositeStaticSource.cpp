#include "CompositeStaticSource.hpp"

#include <algorithm>

void CompositeStaticSource::addSource(IStaticSource* source, int priority) {
  if (source == nullptr) {
    return;
  }

  _sources.push_back(Entry{source, priority});

  // stable_sort - при однаковому priority лишає порядок додавання.
  std::stable_sort(_sources.begin(), _sources.end(),
                   [](const Entry& a, const Entry& b) { return a.priority > b.priority; });
}

IStaticSource* CompositeStaticSource::_findSource(const String& path) const {
  for (const auto& entry : _sources) {
    if (entry.source->exists(path)) {
      return entry.source;
    }
  }

  return nullptr;
}

bool CompositeStaticSource::exists(const String& path) const {
  return _findSource(path) != nullptr;
}

void CompositeStaticSource::handleRequest(AsyncWebServerRequest* request, const String& path) {
  IStaticSource* source = _findSource(path);

  if (source != nullptr) {
    source->handleRequest(request, path);
  }
}
