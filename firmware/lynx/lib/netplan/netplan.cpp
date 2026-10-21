// SPDX-License-Identifier: MIT
#include "netplan.h"

namespace lynx {

NetAction NetPlan::begin(uint32_t now, bool haveCredentials) {
  if (haveCredentials) {
    joining(now);
    return NetAction::None;
  }
  apOpen_ = true;
  return NetAction::OpenAp;
}

void NetPlan::joining(uint32_t now) {
  join_ = JoinState::Connecting;
  joinStart_ = now;
}

NetAction NetPlan::openWindow(uint32_t now, uint32_t windowMs) {
  windowActive_ = true;
  windowUntil_ = now + windowMs;
  if (apOpen_) return NetAction::None;
  apOpen_ = true;
  return NetAction::OpenAp;
}

NetAction NetPlan::tick(uint32_t now, bool staUp) {
  if (staUp && !up_) upSince_ = now;  // every (re)connect restarts the linger clock
  up_ = staUp;

  if (staUp) {
    join_ = JoinState::Connected;
  } else if (join_ == JoinState::Connected) {
    joining(now);  // link dropped; the driver reconnects on its own
  } else if (join_ == JoinState::Connecting && reached(now, joinStart_ + p_.joinTimeoutMs)) {
    join_ = JoinState::Failed;
    if (!apOpen_) {
      apOpen_ = true;
      return NetAction::OpenAp;  // saved network unreachable: offer setup again
    }
  }

  if (windowActive_ && reached(now, windowUntil_)) windowActive_ = false;

  if (apOpen_ && staUp && !windowActive_ && reached(now, upSince_ + p_.lingerMs)) {
    apOpen_ = false;
    return NetAction::CloseAp;
  }
  return NetAction::None;
}

uint32_t NetPlan::closesInMs(uint32_t now) const {
  if (!apOpen_ || !up_) return 0;
  uint32_t t = upSince_ + p_.lingerMs;
  if (windowActive_ && (int32_t)(windowUntil_ - t) > 0) t = windowUntil_;
  return reached(now, t) ? 0 : t - now;
}

}  // namespace lynx
