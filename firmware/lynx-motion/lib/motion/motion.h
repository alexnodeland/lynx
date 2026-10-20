// SPDX-License-Identifier: MIT
// lynx-motion: hardware-independent pan/tilt motion core.
// No Arduino dependencies: compiled for the XIAO and for native unit tests.
#pragma once
#include <stdint.h>

namespace lynx {

enum Axis : uint8_t { PAN = 0, TILT = 1, AXIS_COUNT = 2 };

struct AxisCal {
  float minDeg;      // soft limit (deg from centre)
  float maxDeg;
  float trimUs;      // pulse width at 0 deg
  float usPerDeg;    // pulse change per degree
  bool invert;       // flip direction
  float minUs;       // absolute pulse clamp
  float maxUs;
};

struct MotionConfig {
  float maxSpeedDegPerS;   // peak angular speed limit
  float baseMs;            // fixed part of a move's duration
  float msPerDeg;          // distance-scaled part
  float overshootRatio;    // overshoot as a fraction of distance
  float maxOvershootDeg;   // overshoot cap
  float peakFraction;      // share of the move spent reaching the peak
  uint32_t idleDetachMs;   // stop PWM after this long at rest (0 = never)
};

AxisCal defaultAxisCal(Axis a);
MotionConfig defaultMotionConfig();

// The glance profile, normalised. t in [0,1] -> progress, 0 at t=0, 1 at t=1,
// rises to (1 + overshoot) at t = peak, then settles back to 1. Monotonic in
// each phase, no anticipation dip at the start.
float glance(float t, float overshoot, float peak);

// Ease-in-out cubic, t in [0,1].
float easeInOutCubic(float t);

float clampf(float v, float lo, float hi);

class AxisMotion {
 public:
  void configure(const AxisCal& cal, const MotionConfig& cfg);
  void setCal(const AxisCal& cal) { cal_ = cal; position_ = clampf(position_, cal_.minDeg, cal_.maxDeg); }
  const AxisCal& cal() const { return cal_; }

  // Start an eased move. requestedMs <= 0 means "pick from distance".
  // Returns the duration actually used (may be longer than requested to
  // respect the speed limit). Target is clamped to soft limits.
  uint32_t moveTo(float deg, int32_t requestedMs, uint32_t nowMs);
  void update(uint32_t nowMs);

  float position() const { return position_; }
  float target() const { return target_; }
  bool moving() const { return moving_; }
  bool attached() const { return attached_; }
  uint32_t durationMs() const { return duration_; }
  float overshootDeg() const { return overshootDeg_; }

  // Pulse width for the current position, with trim/invert/clamp applied.
  float pulseUs() const;
  float degToUs(float deg) const;

  // Duration the core would pick for a move of `distDeg` (exposed for tests).
  uint32_t durationFor(float distDeg, int32_t requestedMs) const;

 private:
  AxisCal cal_{};
  MotionConfig cfg_{};
  float position_ = 0, start_ = 0, target_ = 0, overshootDeg_ = 0;
  uint32_t t0_ = 0, duration_ = 0, restSince_ = 0;
  bool moving_ = false, attached_ = false;
};

// Both axes plus a demo sweep sequencer.
class Motion {
 public:
  void begin(const AxisCal& pan, const AxisCal& tilt, const MotionConfig& cfg, uint32_t nowMs);
  uint32_t look(float pan, float tilt, int32_t ms, uint32_t nowMs);  // cancels sweep
  void center(uint32_t nowMs) { look(0, 0, -1, nowMs); }
  void startSweep(uint32_t nowMs);
  bool sweeping() const { return sweepStep_ >= 0; }
  void update(uint32_t nowMs);
  AxisMotion& axis(Axis a) { return axes_[a]; }
  const AxisMotion& axis(Axis a) const { return axes_[a]; }
  MotionConfig& config() { return cfg_; }

 private:
  void sweepStep(uint32_t nowMs);
  AxisMotion axes_[AXIS_COUNT];
  MotionConfig cfg_{};
  int sweepStep_ = -1;
  uint32_t dwellUntil_ = 0;
  bool dwelling_ = false;
};

}  // namespace lynx
