// SPDX-License-Identifier: MIT
#include "auth.h"

#include <cctype>

namespace lynx {

static std::string lower(std::string s) {
  for (char& c : s) c = (char)std::tolower((unsigned char)c);
  return s;
}

std::string tokenFromBytes(const uint8_t bytes[16]) {
  static const char* hex = "0123456789abcdef";
  std::string out(32, '0');
  for (int i = 0; i < 16; i++) {
    out[2 * i] = hex[bytes[i] >> 4];
    out[2 * i + 1] = hex[bytes[i] & 0xf];
  }
  return out;
}

bool tokenEquals(const std::string& a, const std::string& b) {
  if (a.empty() || b.empty()) return false;
  // fold the length difference in, and always walk the expected token's length
  unsigned diff = (unsigned)(a.size() ^ b.size());
  for (size_t i = 0; i < b.size(); i++) diff |= (unsigned char)(i < a.size() ? a[i] : 0) ^ (unsigned char)b[i];
  return diff == 0;
}

bool authorized(const std::string& authorization, const std::string& queryToken, const std::string& token) {
  if (token.empty()) return false;
  if (authorization.size() > 7 && lower(authorization.substr(0, 7)) == "bearer ") {
    std::string t = authorization.substr(7);
    while (!t.empty() && t.front() == ' ') t.erase(0, 1);
    while (!t.empty() && t.back() == ' ') t.pop_back();
    if (tokenEquals(t, token)) return true;
  }
  return !queryToken.empty() && tokenEquals(queryToken, token);
}

std::string originHost(const std::string& origin) {
  size_t p = origin.find("://");
  std::string rest = p == std::string::npos ? origin : origin.substr(p + 3);
  size_t end = rest.find_first_of(":/");
  return lower(end == std::string::npos ? rest : rest.substr(0, end));
}

std::string hostOnly(const std::string& host) {
  size_t c = host.find(':');
  return lower(c == std::string::npos ? host : host.substr(0, c));
}

bool originAllowed(const std::string& origin, const std::string& mdnsName, const std::string& ip) {
  if (origin.empty()) return true;
  std::string h = originHost(origin);
  if (h.empty() || h == "null") return false;
  return h == lower(mdnsName) || (!ip.empty() && h == ip);
}

std::string queryValue(const std::string& query, const std::string& key) {
  size_t i = 0;
  while (i <= query.size()) {
    size_t amp = query.find('&', i);
    std::string pair = query.substr(i, amp == std::string::npos ? std::string::npos : amp - i);
    size_t eq = pair.find('=');
    if (eq != std::string::npos && pair.substr(0, eq) == key) return pair.substr(eq + 1);
    if (amp == std::string::npos) break;
    i = amp + 1;
  }
  return "";
}

}  // namespace lynx
