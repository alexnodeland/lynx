// SPDX-License-Identifier: MIT
// Follow mode: turn the head to keep a detected target (a face) in the middle of the frame.
// Pure logic, unit-tested natively; main.cpp feeds it detections and carries out its moves.
//
// The camera rides on the tilt axis, so a target right of centre needs more pan and one above
// centre needs more tilt (toward lens-up). How the image maps onto the axes depends on how the
// board sits in the pod and on the image flip/mirror settings, so both signs are configurable.
#pragma once
#include <cstdint>

namespace lynx {

struct FollowConfig {
  float hfovDeg = 62.f;      // OV2640 stock lens, horizontal field of view
  float vfovDeg = 48.f;
  float gain = 0.55f;        // fraction of the angular error corrected per update (< 1: no overshoot)
  float deadbandDeg = 2.5f;  // inside this, don't move (no hunting around the centre)
  float maxStepDeg = 12.f;   // per update, per axis
  int panSign = -1;          // pan + turns the head to its left; a target right of centre (unmirrored) is to its right
  int tiltSign = 1;          // +1: target above centre (cy < 0) -> tilt decreases, toward lens-up
  uint32_t holdMs = 2500;    // after losing the target, wait this long where it was last seen...
  uint32_t returnMs = 6000;  // ...then glide back to rest after this long without one
  float restPan = 0.f, restTilt = 45.f;
  float panMin = -88.f, panMax = 88.f, tiltMin = -58.f, tiltMax = 58.f;
};

// A target in normalised image coordinates: cx, cy in [-1, 1], (0, 0) the centre, +x right, +y down.
struct Target {
  float cx = 0, cy = 0;
  float size = 0;  // box area as a fraction of the frame, for choosing among several
  float score = 0;
};

struct FollowMove {
  bool move = false;
  float pan = 0, tilt = 0;
  bool rest = false;  // true when this is the return to the rest pose (use a calm move, not a track)
};

class Follow {
 public:
  explicit Follow(FollowConfig c = FollowConfig()) : c_(c) {}
  FollowConfig& config() { return c_; }

  // A new detection frame. `targets` may be empty. `pan`/`tilt` are the head's current targets.
  FollowMove onFrame(uint32_t now, const Target* targets, int n, float pan, float tilt);
  // Called every loop, so the return to rest happens even when frames stop coming.
  FollowMove tick(uint32_t now, float pan, float tilt);

  void reset() { seenEver_ = false; resting_ = true; }
  bool tracking(uint32_t now) const { return seenEver_ && (int32_t)(now - lastSeen_) < (int32_t)c_.holdMs; }

  // Which of several targets to follow: the biggest, i.e. the nearest face.
  static int pick(const Target* t, int n);

 private:
  static float clamp(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
  float step(float errDeg) const;
  FollowConfig c_;
  bool seenEver_ = false, resting_ = true;
  uint32_t lastSeen_ = 0;
};

}  // namespace lynx
