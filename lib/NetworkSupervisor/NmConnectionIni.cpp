#include "NmConnectionIni.hpp"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace {

std::string trim(const std::string& s) {
  size_t b = 0, e = s.size();
  while (b < e && (s[b] == ' ' || s[b] == '\t' || s[b] == '\r')) ++b;
  while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r')) --e;
  return s.substr(b, e - b);
}

// --- байтова форма keyfile ---------------------------------------------
//
// NetworkManager зберігає значення-масиви (насамперед ssid) як список
// десяткових байтів через ';': ssid=65;115;117;115;32; — саме так виглядає
// "Asus " на справжньому Linux-боксі. Форма потрібна не для краси: інакше
// значущі пробіли на краях (а в цьому проєкті рівно такий SSID і є) гинуть
// при trim(), профіль перестає збігатися зі збереженим і кожен імпорт
// плодить дубль.
bool looksLikeByteArray(const std::string& v) {
  // Одне число без ';' лишаємо рядком: SSID "123" має читатись як "123",
  // а не як байти 1,2,3. Тому вимагаємо хоча б один роздільник.
  if (v.find(';') == std::string::npos) return false;
  bool digit = false;
  for (char c : v) {
    if (c >= '0' && c <= '9') {
      digit = true;
    } else if (c != ';') {
      return false;
    }
  }
  return digit;
}

std::string decodeByteArray(const std::string& v) {
  std::string out;
  size_t pos = 0;
  while (pos < v.size()) {
    const size_t sep = v.find(';', pos);
    const std::string tok = v.substr(pos, (sep == std::string::npos ? v.size() : sep) - pos);
    if (!tok.empty()) {
      const int byte = atoi(tok.c_str());
      if (byte > 0 && byte < 256) out += static_cast<char>(byte);
    }
    if (sep == std::string::npos) break;
    pos = sep + 1;
  }
  return out;
}

// Чи можна писати рядок як є. Пробіли на краях, керівні символи, не-ASCII і
// ';' вимагають байтової форми - інакше файл не переживе зворотного читання.
bool needsByteArray(const std::string& v) {
  if (v.empty()) return false;
  if (v.front() == ' ' || v.front() == '\t') return true;
  if (v.back() == ' ' || v.back() == '\t') return true;
  for (unsigned char c : v) {
    if (c < 0x20 || c > 0x7E || c == ';') return true;
  }
  return false;
}

std::string encodeValue(const std::string& v) {
  if (!needsByteArray(v)) return v;
  std::string out;
  char buf[8];
  for (unsigned char c : v) {
    snprintf(buf, sizeof(buf), "%u;", (unsigned)c);
    out += buf;
  }
  return out;
}

std::string decodeValue(const std::string& v) {
  return looksLikeByteArray(v) ? decodeByteArray(v) : v;
}

bool iniBool(const std::string& v, bool fallback) {
  if (v == "true" || v == "yes" || v == "1") return true;
  if (v == "false" || v == "no" || v == "0") return false;
  return fallback;
}

// "192.168.1.50/24" або "192.168.1.50/24,192.168.1.1" (форма NetworkManager,
// де шлюз дописано через кому) -> адреса / маска / шлюз.
void parseAddress(const std::string& value, std::string& ip, std::string& mask,
                  std::string& gateway) {
  std::string addr = value;

  const size_t comma = addr.find(',');
  if (comma != std::string::npos) {
    const std::string gw = trim(addr.substr(comma + 1));
    if (!gw.empty()) gateway = gw;
    addr = addr.substr(0, comma);
  }
  addr = trim(addr);

  const size_t slash = addr.find('/');
  if (slash == std::string::npos) {
    ip = addr;
    mask = NmConnectionIni::cidrToMask(24);
    return;
  }
  ip = trim(addr.substr(0, slash));
  mask = NmConnectionIni::cidrToMask(atoi(addr.substr(slash + 1).c_str()));
}

}  // namespace

namespace NmConnectionIni {

std::string cidrToMask(int bits) {
  if (bits < 0 || bits > 32) bits = 24;
  const uint32_t mask = (bits == 0) ? 0u : (0xFFFFFFFFu << (32 - bits));
  char buf[16];
  snprintf(buf, sizeof(buf), "%u.%u.%u.%u", (unsigned)((mask >> 24) & 0xFF),
           (unsigned)((mask >> 16) & 0xFF), (unsigned)((mask >> 8) & 0xFF),
           (unsigned)(mask & 0xFF));
  return std::string(buf);
}

int maskToCidr(const std::string& mask) {
  unsigned a = 0, b = 0, c = 0, d = 0;
  if (sscanf(mask.c_str(), "%u.%u.%u.%u", &a, &b, &c, &d) != 4) return 24;
  uint32_t m = (a << 24) | (b << 16) | (c << 8) | d;
  int bits = 0;
  while (m & 0x80000000u) {
    ++bits;
    m <<= 1;
  }
  return bits;
}

bool parse(const std::string& text, WifiConnection& out, std::string* error) {
  std::string section;
  bool haveSsid = false;

  // Значення з [connection] застосовуємо навіть якщо секція йде ПІСЛЯ [wifi],
  // тому просто накопичуємо в out і перевіряємо ssid у кінці.
  size_t pos = 0;
  while (pos <= text.size()) {
    size_t nl = text.find('\n', pos);
    if (nl == std::string::npos) nl = text.size();
    const std::string line = trim(text.substr(pos, nl - pos));
    pos = nl + 1;

    if (line.empty() || line[0] == '#' || line[0] == ';') continue;

    if (line[0] == '[') {
      const size_t close = line.find(']');
      if (close == std::string::npos) continue;
      section = line.substr(1, close - 1);
      continue;
    }

    const size_t eq = line.find('=');
    if (eq == std::string::npos) continue;
    const std::string key = trim(line.substr(0, eq));
    const std::string value = trim(line.substr(eq + 1));

    if (section == "connection") {
      if (key == "autoconnect") {
        out.isEnabled = iniBool(value, true);
      } else if (key == "autoconnect-priority") {
        out.priority = static_cast<int8_t>(atoi(value.c_str()));
      } else if (key == "autoconnect-retries") {
        out.maxRetries = static_cast<int8_t>(atoi(value.c_str()));
      } else if (key == "id" && out.ssid.empty()) {
        // Запасний варіант: у NetworkManager id зазвичай дорівнює SSID.
        // Справжній wifi.ssid нижче його перекриє.
        out.ssid = decodeValue(value);
      } else if (key == "type" && !value.empty() && value != "wifi" &&
                 value != "802-11-wireless") {
        if (error) *error = "not a wifi connection (type=" + value + ")";
        return false;
      }
    } else if (section == "wifi" || section == "802-11-wireless") {
      if (key == "ssid") {
        out.ssid = decodeValue(value);
        haveSsid = !out.ssid.empty();
      }
    } else if (section == "wifi-security" || section == "802-11-wireless-security") {
      if (key == "psk") out.password = decodeValue(value);
    } else if (section == "ipv4") {
      if (key == "method") {
        out.staticIp = (value == "manual");
      } else if (key == "address1" || key == "addresses") {
        parseAddress(value, out.ip, out.subnet, out.gateway);
      } else if (key == "gateway") {
        out.gateway = value;
      } else if (key == "dns") {
        // NetworkManager пише список через ';' і лишає ';' у кінці.
        // Пристрій вміє рівно один резолвер - беремо перший.
        const size_t sep = value.find(';');
        out.dns = (sep == std::string::npos) ? value : value.substr(0, sep);
      }
    }
  }

  if (!haveSsid && out.ssid.empty()) {
    if (error) *error = "no wifi.ssid";
    return false;
  }
  // method=manual без адреси - конфіг, який нікуди не підключиться.
  // Тихо відкочуємо на DHCP, а не тягнемо в NVS зламаний профіль.
  if (out.staticIp && out.ip.empty()) out.staticIp = false;
  return true;
}

std::string serialize(const WifiConnection& conn) {
  std::string s;
  s += "[connection]\n";
  s += "id=" + encodeValue(conn.ssid) + "\n";
  s += "type=wifi\n";
  s += std::string("autoconnect=") + (conn.isEnabled ? "true" : "false") + "\n";
  s += "autoconnect-priority=" + std::to_string((int)conn.priority) + "\n";
  s += "autoconnect-retries=" + std::to_string((int)conn.maxRetries) + "\n";
  s += "\n";

  s += "[wifi]\n";
  s += "mode=infrastructure\n";
  s += "ssid=" + encodeValue(conn.ssid) + "\n";
  s += "\n";

  if (!conn.password.empty()) {
    s += "[wifi-security]\n";
    s += "key-mgmt=wpa-psk\n";
    s += "psk=" + encodeValue(conn.password) + "\n";
    s += "\n";
  }

  s += "[ipv4]\n";
  if (conn.staticIp && !conn.ip.empty()) {
    s += "method=manual\n";
    s += "address1=" + conn.ip + "/" + std::to_string(maskToCidr(conn.subnet));
    if (!conn.gateway.empty()) s += "," + conn.gateway;
    s += "\n";
    if (!conn.gateway.empty()) s += "gateway=" + conn.gateway + "\n";
    if (!conn.dns.empty()) s += "dns=" + conn.dns + ";\n";
  } else {
    s += "method=auto\n";
  }
  s += "\n";

  s += "[ipv6]\n";
  s += "method=ignore\n";
  return s;
}

std::string fileNameFor(const WifiConnection& conn, size_t maxLen) {
  static const char kExt[] = ".nmconnection";
  const size_t extLen = sizeof(kExt) - 1;

  std::string base = conn.ssid.empty() ? std::string("unnamed") : conn.ssid;
  for (auto& c : base) {
    // '/' зламав би шлях, решта - просто щоб ім'я лишалось набірним з консолі.
    if (c == '/' || c == '\\' || c == ':' || c == ' ') c = '_';
  }

  if (maxLen > extLen && base.size() > maxLen - extLen) {
    base.resize(maxLen - extLen);
  }
  return base + kExt;
}

}  // namespace NmConnectionIni
