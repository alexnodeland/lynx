// SPDX-License-Identifier: MIT
// Access control for the LAN API: a per-device bearer token and an Origin check.
// Pure logic, no Arduino dependencies, so it is unit-tested natively.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

namespace lynx {

// 32 lowercase hex characters from 16 random bytes.
std::string tokenFromBytes(const uint8_t bytes[16]);

// Constant-time comparison: the time taken does not depend on where the strings differ.
bool tokenEquals(const std::string& a, const std::string& b);

// True when `authorization` is "Bearer <token>" (scheme case-insensitive) or
// `queryToken` (the ?token= value, for <img src> and stream URLs) matches.
bool authorized(const std::string& authorization, const std::string& queryToken, const std::string& token);

// Host part of an Origin header ("http://lynx-1a2b.local:80" -> "lynx-1a2b.local"), lowercased.
std::string originHost(const std::string& origin);

// Host header without port, lowercased.
std::string hostOnly(const std::string& host);

// MCP's DNS-rebinding guard: an absent Origin is fine (non-browser clients); a present one must
// name this device by its mDNS name or IP. Never compare against the Host header: a rebinding
// page sends Host and Origin for its own domain, and both would match.
bool originAllowed(const std::string& origin, const std::string& mdnsName, const std::string& ip);

// Value of `key` in a raw query string ("a=1&token=abc" -> "abc"); empty if absent. No URL decoding
// beyond what tokens need (they are hex).
std::string queryValue(const std::string& query, const std::string& key);

// Slows down token guessing. A 128-bit token can't be brute-forced, so this is about noise and load:
// after kFree wrong tokens from one address, that address is refused (HTTP 429) for a lockout that
// doubles with each further failure, from 1 s up to 5 min. A right token clears its record.
// Fixed memory: the least recently seen address is forgotten when the table is full.
// Not thread-safe; the firmware wraps it in a mutex (port 80 and port 81 run in different tasks).
class AuthThrottle {
 public:
  static constexpr int kSlots = 8;
  static constexpr uint32_t kFree = 5;           // wrong tokens allowed before any lockout
  static constexpr uint32_t kBaseMs = 1000;      // first lockout
  static constexpr uint32_t kMaxMs = 300000;     // longest lockout
  static constexpr uint32_t kForgetMs = 600000;  // a quiet address starts over after 10 min

  // 0 if `ip` may try now, else milliseconds until it may.
  uint32_t retryAfter(uint32_t ip, uint32_t nowMs) const;
  void failure(uint32_t ip, uint32_t nowMs);
  void success(uint32_t ip);
  uint32_t failures(uint32_t ip) const;

 private:
  struct Slot {
    uint32_t ip = 0, fails = 0, lastMs = 0, untilMs = 0;
    bool used = false;
  };
  Slot slots_[kSlots];
  const Slot* find(uint32_t ip) const;
};

}  // namespace lynx
