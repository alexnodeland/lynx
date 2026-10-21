// SPDX-License-Identifier: MIT
// When the setup hotspot opens and closes, and the join state the setup page reports.
// Pure logic over millis(), wrap-safe, so the rules are unit-tested natively; net.cpp only
// carries out the actions.
//
// Rules:
//  - No saved network: the hotspot is open and stays open until a network is joined.
//  - Joining a network: once it has been connected for `lingerMs` (so the setup page can show
//    the result), the hotspot closes, unless a manual window (BOOT held) is still running.
//  - Saved network unreachable for `joinTimeoutMs`: the hotspot opens (and the join keeps retrying).
//  - Manual window: open for `windowMs`, then close once the network is up.
#pragma once
#include <cstdint>

namespace lynx {

enum class JoinState { Idle, Connecting, Connected, Failed };
enum class NetAction { None, OpenAp, CloseAp };

struct NetPolicy {
  uint32_t lingerMs = 180000;
  uint32_t joinTimeoutMs = 20000;
};

class NetPlan {
 public:
  explicit NetPlan(NetPolicy p = NetPolicy()) : p_(p) {}

  // Boot. Returns OpenAp when there is no saved network.
  NetAction begin(uint32_t now, bool haveCredentials);
  // A join was started (setup page or serial).
  void joining(uint32_t now);
  // Hold the hotspot open for at least `windowMs` (BOOT button, serial `setup`).
  NetAction openWindow(uint32_t now, uint32_t windowMs);
  // Call every loop with the station's link state.
  NetAction tick(uint32_t now, bool staUp);

  bool apOpen() const { return apOpen_; }
  JoinState join() const { return join_; }
  // ms until the hotspot may close (0 = it may close now, or it isn't open); for status/debug.
  uint32_t closesInMs(uint32_t now) const;

 private:
  static bool reached(uint32_t now, uint32_t t) { return (int32_t)(now - t) >= 0; }
  NetPolicy p_;
  bool apOpen_ = false;
  bool windowActive_ = false;
  uint32_t windowUntil_ = 0;
  JoinState join_ = JoinState::Idle;
  uint32_t joinStart_ = 0;
  bool up_ = false;
  uint32_t upSince_ = 0;
};

}  // namespace lynx
