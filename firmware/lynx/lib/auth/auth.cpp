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

// ---------- AuthThrottle ----------

const AuthThrottle::Slot* AuthThrottle::find(uint32_t ip) const {
  for (const Slot& s : slots_)
    if (s.used && s.ip == ip) return &s;
  return nullptr;
}

uint32_t AuthThrottle::retryAfter(uint32_t ip, uint32_t nowMs) const {
  const Slot* s = find(ip);
  if (!s || nowMs - s->lastMs >= kForgetMs) return 0;
  int32_t left = (int32_t)(s->untilMs - nowMs);  // wrap-safe
  return left > 0 ? (uint32_t)left : 0;
}

void AuthThrottle::failure(uint32_t ip, uint32_t nowMs) {
  Slot* s = const_cast<Slot*>(find(ip));
  if (!s) {  // a free slot, else the one seen longest ago
    s = &slots_[0];
    for (Slot& c : slots_) {
      if (!c.used) { s = &c; break; }
      if (nowMs - c.lastMs > nowMs - s->lastMs) s = &c;
    }
    *s = Slot();
    s->used = true;
    s->ip = ip;
  } else if (nowMs - s->lastMs >= kForgetMs) {
    s->fails = 0;
  }
  s->fails++;
  s->lastMs = nowMs;
  if (s->fails > kFree) {
    uint32_t n = s->fails - kFree - 1, ms = kBaseMs;
    while (n-- && ms < kMaxMs) ms *= 2;
    s->untilMs = nowMs + (ms < kMaxMs ? ms : kMaxMs);
  }
}

void AuthThrottle::success(uint32_t ip) {
  Slot* s = const_cast<Slot*>(find(ip));
  if (s) *s = Slot();
}

uint32_t AuthThrottle::failures(uint32_t ip) const {
  const Slot* s = find(ip);
  return s ? s->fails : 0;
}

}  // namespace lynx
