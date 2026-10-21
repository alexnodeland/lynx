// SPDX-License-Identifier: MIT
// A small, stateless MCP server (tools only) over Streamable HTTP.
//
// Serves both protocol generations from one endpoint:
//  - 2026-07-28: stateless; every request carries params._meta["io.modelcontextprotocol/protocolVersion"],
//    discovery is server/discover, headers MCP-Protocol-Version / Mcp-Method / Mcp-Name must agree with the body.
//  - 2025-11-25 / 2025-06-18 / 2025-03-26: the initialize handshake. No session ids are minted.
//
// Pure logic over a Device interface, so it is unit-tested natively with a fake device.
#pragma once
#include <ArduinoJson.h>

#include <cstdint>
#include <string>

namespace lynx {

struct McpRequest {
  std::string body;
  std::string protocolHeader;  // MCP-Protocol-Version
  std::string methodHeader;    // Mcp-Method
  std::string nameHeader;      // Mcp-Name
};

struct McpResponse {
  int status = 200;  // 202 for accepted notifications (empty body)
  std::string body;
};

// What the MCP tools act on. The firmware implements it over the servos and camera.
class Device {
 public:
  virtual ~Device() = default;
  virtual std::string name() = 0;     // e.g. "lynx-1a2b"
  virtual std::string version() = 0;  // firmware version
  virtual void status(JsonObject out) = 0;
  virtual uint32_t look(float pan, float tilt, int32_t ms) = 0;  // returns the move's duration
  virtual void center() = 0;
  virtual void sweep() = 0;
  virtual bool snapshot(std::string& jpeg, std::string& error) = 0;
  virtual bool setCamera(JsonObjectConst settings, std::string& error) = 0;
  virtual std::string snapshotUrl() = 0;
  virtual std::string streamUrl() = 0;
};

constexpr const char* kModernVersion = "2026-07-28";
bool isLegacyVersion(const std::string& v);

McpResponse handleMcp(Device& dev, const McpRequest& req);

std::string base64(const uint8_t* data, size_t len);

}  // namespace lynx
