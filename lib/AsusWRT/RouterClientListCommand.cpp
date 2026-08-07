#include "RouterClientListCommand.hpp"

#include <vector>

#include "RouterApiClient.hpp"
#include "RouterClientInfo.hpp"
#include "RouterClientListFormatter.hpp"
#include "RouterClientListParser.hpp"
#include "TLogger.hpp"

namespace {
const TLogger kLogger{"router"};

// Спільна частина для обох публічних функцій: login + fetch + parse.
// Повертає false і логує помилку сама (обидва виклики просто прокидають false далі).
bool loginFetchParse(const String& host, const String& loginAuthorization,
                      std::vector<RouterClientInfo>& outClients) {
  RouterApiClient api(host, loginAuthorization);

  if (!api.login()) {
    kLogger.error("login failed: %s", api.lastError());
    return false;
  }

  String json;
  if (!api.fetchClientListJson(json)) {
    kLogger.error("fetch failed: %s", api.lastError());
    return false;
  }

  if (!RouterClientListParser::parse(json, outClients)) {
    kLogger.error("parse failed or out of memory, got %u clients", (unsigned)outClients.size());
    return false;
  }

  return true;
}
}  // namespace

bool fetchRouterClientListTable(const String& host, const String& loginAuthorization, char* buffer,
                                 size_t bufferSize) {
  std::vector<RouterClientInfo> clients;
  if (!loginFetchParse(host, loginAuthorization, clients)) return false;

  size_t written = 0;
  if (!RouterClientListFormatter::format(clients, buffer, bufferSize, written)) {
    kLogger.error("buffer too small, need >= %u bytes, got %u clients", (unsigned)written,
                   (unsigned)clients.size());
    return false;
  }

  return true;
}

bool fetchRouterClientListIterator(const String& host, const String& loginAuthorization,
                                    RouterClientListIterator& outIterator) {
  std::vector<RouterClientInfo> clients;
  if (!loginFetchParse(host, loginAuthorization, clients)) return false;

  outIterator.assign(std::move(clients));
  return true;
}
