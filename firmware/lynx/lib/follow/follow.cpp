// SPDX-License-Identifier: MIT
#include "follow.h"

#include <cmath>

namespace lynx {

int Follow::pick(const Target* t, int n) {
  int best = -1;
  for (int i = 0; i < n; i++)
    if (best < 0 || t[i].size > t[best].size) best = i;
  return best;
}

float Follow::step(float errDeg) const {
  if (std::fabs(errDeg) < c_.deadbandDeg) return 0.f;
  return clamp(errDeg * c_.gain, -c_.maxStepDeg, c_.maxStepDeg);
}

FollowMove Follow::onFrame(uint32_t now, const Target* targets, int n, float pan, float tilt) {
  int i = pick(targets, n);
  if (i < 0) return tick(now, pan, tilt);
  seenEver_ = true;
  resting_ = false;
  lastSeen_ = now;
  // angular error of the target from the optical axis (small-angle: proportional to the offset)
  float ex = targets[i].cx * c_.hfovDeg * 0.5f;
  float ey = targets[i].cy * c_.vfovDeg * 0.5f;
  float dp = step(ex) * (float)c_.panSign;
  float dt = step(ey) * (float)c_.tiltSign;
  FollowMove m;
  if (dp == 0.f && dt == 0.f) return m;  // centred enough
  m.move = true;
  m.pan = clamp(pan + dp, c_.panMin, c_.panMax);
  m.tilt = clamp(tilt + dt, c_.tiltMin, c_.tiltMax);
  if (m.pan == pan && m.tilt == tilt) m.move = false;  // pinned at a limit
  return m;
}

FollowMove Follow::tick(uint32_t now, float pan, float tilt) {
  FollowMove m;
  if (resting_ || !seenEver_) return m;
  if ((int32_t)(now - lastSeen_) < (int32_t)c_.returnMs) return m;  // still holding where it was seen
  resting_ = true;
  if (pan == c_.restPan && tilt == c_.restTilt) return m;
  m.move = true;
  m.rest = true;
  m.pan = c_.restPan;
  m.tilt = c_.restTilt;
  return m;
}

}  // namespace lynx
