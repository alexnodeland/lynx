// SPDX-License-Identifier: MIT
// HTTP on port 80: pages, REST API, MCP, snapshots. Port 81 (video + events) is stream.cpp.
#pragma once
#include <cstdint>
#include <string>

namespace lynx {

void webBegin();

// The token check both servers share, throttled per client address (see AuthThrottle).
enum class Auth { Ok, Denied, Throttled };
Auth checkToken(int fd, const std::string& authorization, const std::string& queryToken, uint32_t& retrySeconds);

}  // namespace lynx
