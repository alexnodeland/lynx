// SPDX-License-Identifier: MIT
#include "motion.h"

#include <math.h>

namespace lynx {

AxisCal defaultAxisCal(Axis a) {
  AxisCal c;
  // Mechanical limits (CAD): pan +/-90, tilt +/-60. Soft limits sit 2 deg inside.
  c.minDeg = (a == PAN) ? -88.f : -58.f;
  c.maxDeg = (a == PAN) ? 88.f : 58.f;
  c.trimUs = 1500.f;
  c.usPerDeg = 2000.f / 180.f;  // 500..2500 us over 180 deg
  c.invert = false;
  c.minUs = 500.f;
  c.maxUs = 2500.f;
  return c;
}

MotionConfig defaultMotionConfig() {
  MotionConfig m;
  m.maxSpeedDegPerS = 300.f;  // MG90S no-load ~600 deg/s; stay well under
  m.baseMs = 180.f;
  m.msPerDeg = 4.f;
  m.overshootRatio = 0.06f;
  m.maxOvershootDeg = 4.f;
  m.peakFraction = 0.75f;
  m.idleDetachMs = 1500;
  return m;
}

float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

float easeInOutCubic(float t) {
  t = clampf(t, 0.f, 1.f);
  return t < 0.5f ? 4.f * t * t * t : 1.f - powf(-2.f * t + 2.f, 3.f) / 2.f;
}

float easeOutCubic(float t) {
  t = clampf(t, 0.f, 1.f);
  float u = 1.f - t;
  return 1.f - u * u * u;
}

float glance(float t, float overshoot, float peak) {
  t = clampf(t, 0.f, 1.f);
  peak = clampf(peak, 0.05f, 1.f);
  if (overshoot <= 0.f || peak >= 1.f) return easeInOutCubic(t);
  if (t <= peak) return (1.f + overshoot) * easeInOutCubic(t / peak);
  return 1.f + overshoot - overshoot * easeInOutCubic((t - peak) / (1.f - peak));
}

void AxisMotion::configure(const AxisCal& cal, const MotionConfig& cfg) {
  cal_ = cal;
  cfg_ = cfg;
  position_ = start_ = target_ = clampf(0.f, cal_.minDeg, cal_.maxDeg);
}

uint32_t AxisMotion::durationFor(float dist, int32_t requestedMs, MoveStyle style) const {
  dist = fabsf(dist);
  if (dist < 0.01f) return 0;
  float ov = style == MoveStyle::Track ? 0.f : fminf(dist * cfg_.overshootRatio, cfg_.maxOvershootDeg) / dist;
  float peak = (ov > 0.f) ? cfg_.peakFraction : 1.f;
  // both curves peak at slope 3: easeInOutCubic at t=0.5, easeOutCubic at t=0
  // easeInOutCubic peak slope is 3 (at t=0.5); phase 1 covers (1+ov)*dist in peak*D.
  float minMs = 1000.f * 3.f * (1.f + ov) * dist / (peak * cfg_.maxSpeedDegPerS);
  float want = requestedMs > 0 ? (float)requestedMs : cfg_.baseMs + cfg_.msPerDeg * dist;
  return (uint32_t)ceilf(fmaxf(want, minMs));
}

uint32_t AxisMotion::moveTo(float deg, int32_t requestedMs, uint32_t nowMs, MoveStyle style) {
  update(nowMs);
  target_ = clampf(deg, cal_.minDeg, cal_.maxDeg);
  start_ = position_;
  float dist = fabsf(target_ - start_);
  style_ = style;
  overshootDeg_ = style == MoveStyle::Track ? 0.f : fminf(dist * cfg_.overshootRatio, cfg_.maxOvershootDeg);
  duration_ = durationFor(dist, requestedMs, style);
  t0_ = nowMs;
  attached_ = true;
  restSince_ = nowMs;
  moving_ = duration_ > 0;
  if (!moving_) position_ = target_;
  return duration_;
}

void AxisMotion::update(uint32_t nowMs) {
  if (moving_) {
    uint32_t el = nowMs - t0_;  // wrap-safe
    if (el >= duration_) {
      position_ = target_;
      moving_ = false;
      restSince_ = nowMs;
    } else {
      float dist = target_ - start_;
      float ov = fabsf(dist) > 0.f ? overshootDeg_ / fabsf(dist) : 0.f;
      float t = (float)el / (float)duration_;
      float p = style_ == MoveStyle::Track ? easeOutCubic(t) : glance(t, ov, cfg_.peakFraction);
      position_ = clampf(start_ + dist * p, cal_.minDeg, cal_.maxDeg);
    }
  } else if (attached_ && cfg_.idleDetachMs > 0 && (nowMs - restSince_) >= cfg_.idleDetachMs) {
    attached_ = false;
  }
}

float AxisMotion::degToUs(float deg) const {
  float d = cal_.invert ? -deg : deg;
  return clampf(cal_.trimUs + d * cal_.usPerDeg, cal_.minUs, cal_.maxUs);
}

float AxisMotion::pulseUs() const { return degToUs(position_); }

void Motion::begin(const AxisCal& pan, const AxisCal& tilt, const MotionConfig& cfg, uint32_t nowMs) {
  cfg_ = cfg;
  axes_[PAN].configure(pan, cfg);
  axes_[TILT].configure(tilt, cfg);
  sweepStep_ = -1;
  center(nowMs);
}

uint32_t Motion::look(float pan, float tilt, int32_t ms, uint32_t nowMs) {
  sweepStep_ = -1;
  dwelling_ = false;
  uint32_t a = axes_[PAN].moveTo(pan, ms, nowMs);
  uint32_t b = axes_[TILT].moveTo(tilt, ms, nowMs);
  // Keep both axes in step: re-issue the shorter with the longer duration.
  uint32_t d = a > b ? a : b;
  if (d > 0) {
    axes_[PAN].moveTo(pan, (int32_t)d, nowMs);
    axes_[TILT].moveTo(tilt, (int32_t)d, nowMs);
  }
  return d;
}

uint32_t Motion::track(float pan, float tilt, int32_t ms, uint32_t nowMs) {
  sweepStep_ = -1;
  dwelling_ = false;
  uint32_t a = axes_[PAN].moveTo(pan, ms, nowMs, MoveStyle::Track);
  uint32_t b = axes_[TILT].moveTo(tilt, ms, nowMs, MoveStyle::Track);
  uint32_t d = a > b ? a : b;
  if (d > 0) {
    axes_[PAN].moveTo(pan, (int32_t)d, nowMs, MoveStyle::Track);
    axes_[TILT].moveTo(tilt, (int32_t)d, nowMs, MoveStyle::Track);
  }
  return d;
}

// Sweep waypoints as fractions of each axis's soft-limit range.
static const float kSweep[][2] = {
    {0, 0}, {-1, 0}, {1, 0}, {0, 0}, {0, 1}, {0, -1}, {-1, 1}, {1, 1}, {1, -1}, {-1, -1}, {0, 0}};
static const int kSweepLen = sizeof(kSweep) / sizeof(kSweep[0]);
static const uint32_t kDwellMs = 400;

void Motion::startSweep(uint32_t nowMs) {
  look(0, 0, -1, nowMs);
  sweepStep_ = 0;
  dwelling_ = false;
}

void Motion::sweepStep(uint32_t nowMs) {
  if (axes_[PAN].moving() || axes_[TILT].moving()) return;
  if (!dwelling_) {
    dwelling_ = true;
    dwellUntil_ = nowMs + kDwellMs;
    return;
  }
  if ((int32_t)(nowMs - dwellUntil_) < 0) return;
  dwelling_ = false;
  int next = sweepStep_ + 1;
  if (next >= kSweepLen) {
    center(nowMs);  // end at the rest pose, not lens-up (resets sweepStep_)
    return;
  }
  auto frac = [](const AxisCal& c, float f) { return f < 0 ? -f * c.minDeg : f * c.maxDeg; };
  float p = frac(axes_[PAN].cal(), kSweep[next][0]);
  float t = frac(axes_[TILT].cal(), kSweep[next][1]);
  look(p, t, -1, nowMs);  // resets sweepStep_
  sweepStep_ = next;
}

void Motion::update(uint32_t nowMs) {
  axes_[PAN].update(nowMs);
  axes_[TILT].update(nowMs);
  if (sweepStep_ >= 0) sweepStep(nowMs);
}

}  // namespace lynx
