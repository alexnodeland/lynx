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

}  // namespace lynx
