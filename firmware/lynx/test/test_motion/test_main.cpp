// SPDX-License-Identifier: MIT
#include <math.h>
#include <unity.h>

#include "motion.h"

using namespace lynx;

void setUp() {}
void tearDown() {}

static AxisMotion makeAxis(Axis a, MotionConfig cfg = defaultMotionConfig()) {
  AxisMotion m;
  m.configure(defaultAxisCal(a), cfg);
  return m;
}

static void test_default_soft_limits_inside_mechanical() {
  AxisCal p = defaultAxisCal(PAN), t = defaultAxisCal(TILT);
  TEST_ASSERT_TRUE(p.minDeg > -90 && p.maxDeg < 90);
  TEST_ASSERT_TRUE(t.minDeg > -60 && t.maxDeg < 60);
  TEST_ASSERT_EQUAL_FLOAT(88, p.maxDeg);
  TEST_ASSERT_EQUAL_FLOAT(-58, t.minDeg);
}

static void test_clamps_target_to_soft_limits() {
  AxisMotion pan = makeAxis(PAN), tilt = makeAxis(TILT);
  pan.moveTo(170, -1, 0);
  tilt.moveTo(-100, -1, 0);
  TEST_ASSERT_EQUAL_FLOAT(88, pan.target());
  TEST_ASSERT_EQUAL_FLOAT(-58, tilt.target());
  pan.update(100000);
  TEST_ASSERT_EQUAL_FLOAT(88, pan.position());
}

static void test_position_never_exceeds_limits_during_overshoot() {
  AxisMotion pan = makeAxis(PAN);
  uint32_t d = pan.moveTo(88, -1, 0);
  for (uint32_t t = 0; t <= d; t += 2) {
    pan.update(t);
    TEST_ASSERT_TRUE(pan.position() <= 88.0001f);
  }
}

static void test_easing_endpoints() {
  TEST_ASSERT_FLOAT_WITHIN(1e-6, 0, easeInOutCubic(0));
  TEST_ASSERT_FLOAT_WITHIN(1e-6, 1, easeInOutCubic(1));
  TEST_ASSERT_FLOAT_WITHIN(1e-6, 0.5, easeInOutCubic(0.5));
  TEST_ASSERT_FLOAT_WITHIN(1e-6, 0, glance(0, 0.05f, 0.75f));
  TEST_ASSERT_FLOAT_WITHIN(1e-6, 1, glance(1, 0.05f, 0.75f));
  TEST_ASSERT_FLOAT_WITHIN(1e-6, 1.05f, glance(0.75f, 0.05f, 0.75f));
  // out-of-range t is clamped
  TEST_ASSERT_FLOAT_WITHIN(1e-6, 0, glance(-1, 0.05f, 0.75f));
  TEST_ASSERT_FLOAT_WITHIN(1e-6, 1, glance(2, 0.05f, 0.75f));
}

static void test_glance_no_anticipation_and_overshoot_bounded() {
  float maxv = 0;
  for (int i = 0; i <= 1000; i++) {
    float v = glance(i / 1000.f, 0.06f, 0.75f);
    TEST_ASSERT_TRUE(v >= 0.f);
    if (v > maxv) maxv = v;
  }
  TEST_ASSERT_FLOAT_WITHIN(1e-4, 1.06f, maxv);
}

static void test_overshoot_capped_in_degrees() {
  AxisMotion pan = makeAxis(PAN);
  pan.moveTo(-80, -1, 0);
  pan.update(100000);
  uint32_t d = pan.moveTo(80, -1, 100000);  // 160 deg move
  MotionConfig c = defaultMotionConfig();
  TEST_ASSERT_FLOAT_WITHIN(1e-4, c.maxOvershootDeg, pan.overshootDeg());
  float maxPos = -1000;
  for (uint32_t t = 0; t <= d; t++) {
    pan.update(100000 + t);
    if (pan.position() > maxPos) maxPos = pan.position();
  }
  TEST_ASSERT_TRUE(maxPos > 80.f);
  TEST_ASSERT_TRUE(maxPos <= 80.f + c.maxOvershootDeg + 1e-3f);
}

static void test_duration_scales_with_distance() {
  AxisMotion pan = makeAxis(PAN);
  uint32_t small = pan.durationFor(10, -1);
  uint32_t mid = pan.durationFor(45, -1);
  uint32_t big = pan.durationFor(170, -1);
  TEST_ASSERT_TRUE(small < mid);
  TEST_ASSERT_TRUE(mid < big);
  TEST_ASSERT_EQUAL_UINT32(0, pan.durationFor(0, -1));
}

static void test_requested_duration_honoured_unless_too_fast() {
  AxisMotion pan = makeAxis(PAN);
  TEST_ASSERT_EQUAL_UINT32(2000, pan.durationFor(30, 2000));
  // 170 deg in 50 ms would exceed max speed -> stretched
  uint32_t d = pan.durationFor(170, 50);
  TEST_ASSERT_TRUE(d > 50);
}

static void test_peak_speed_respects_limit() {
  MotionConfig c = defaultMotionConfig();
  AxisMotion pan = makeAxis(PAN, c);
  pan.moveTo(-88, -1, 0);
  pan.update(100000);
  uint32_t d = pan.moveTo(88, 10, 100000);  // ask for absurdly fast
  float prev = pan.position(), maxSpeed = 0;
  for (uint32_t t = 1; t <= d; t++) {
    pan.update(100000 + t);
    float v = fabsf(pan.position() - prev) * 1000.f;  // deg/s over 1 ms
    if (v > maxSpeed) maxSpeed = v;
    prev = pan.position();
  }
  TEST_ASSERT_TRUE(maxSpeed <= c.maxSpeedDegPerS * 1.02f);
}

static void test_idle_detach_timing() {
  MotionConfig c = defaultMotionConfig();
  c.idleDetachMs = 1000;
  AxisMotion pan = makeAxis(PAN, c);
  TEST_ASSERT_FALSE(pan.attached());
  uint32_t d = pan.moveTo(30, 500, 0);
  TEST_ASSERT_EQUAL_UINT32(500, d);
  TEST_ASSERT_TRUE(pan.attached());
  pan.update(d);  // arrives
  TEST_ASSERT_FALSE(pan.moving());
  pan.update(d + 999);
  TEST_ASSERT_TRUE(pan.attached());
  pan.update(d + 1000);
  TEST_ASSERT_FALSE(pan.attached());
  pan.moveTo(-10, -1, d + 2000);  // reattaches
  TEST_ASSERT_TRUE(pan.attached());
}

static void test_idle_detach_survives_millis_wrap() {
  MotionConfig c = defaultMotionConfig();
  c.idleDetachMs = 1000;
  AxisMotion pan = makeAxis(PAN, c);
  uint32_t t0 = 0xFFFFFF00u;
  uint32_t d = pan.moveTo(20, 400, t0);
  pan.update(t0 + d);  // wraps
  TEST_ASSERT_FALSE(pan.moving());
  TEST_ASSERT_EQUAL_FLOAT(20, pan.position());
  pan.update(t0 + d + 500);
  TEST_ASSERT_TRUE(pan.attached());
  pan.update(t0 + d + 1000);
  TEST_ASSERT_FALSE(pan.attached());
}

static void test_idle_detach_disabled() {
  MotionConfig c = defaultMotionConfig();
  c.idleDetachMs = 0;
  AxisMotion pan = makeAxis(PAN, c);
  pan.moveTo(10, 100, 0);
  pan.update(10000000);
  TEST_ASSERT_TRUE(pan.attached());
}

static void test_pulse_trim_invert_and_clamp() {
  AxisMotion pan = makeAxis(PAN);
  TEST_ASSERT_FLOAT_WITHIN(0.01, 1500, pan.degToUs(0));
  TEST_ASSERT_FLOAT_WITHIN(0.01, 2500, pan.degToUs(90));
  AxisCal c = defaultAxisCal(PAN);
  c.trimUs = 1520;
  c.invert = true;
  pan.setCal(c);
  TEST_ASSERT_FLOAT_WITHIN(0.01, 1520, pan.degToUs(0));
  TEST_ASSERT_TRUE(pan.degToUs(10) < 1520);
  c.trimUs = 2400;
  c.invert = false;
  pan.setCal(c);
  TEST_ASSERT_FLOAT_WITHIN(0.01, 2500, pan.degToUs(88));  // clamped to maxUs
}

static void test_look_syncs_axes_and_sweep_completes() {
  Motion m;
  m.begin(defaultAxisCal(PAN), defaultAxisCal(TILT), defaultMotionConfig(), 0);
  uint32_t d = m.look(60, 5, -1, 0);
  TEST_ASSERT_EQUAL_UINT32(d, m.axis(PAN).durationMs());
  TEST_ASSERT_EQUAL_UINT32(d, m.axis(TILT).durationMs());
  m.startSweep(10000);
  TEST_ASSERT_TRUE(m.sweeping());
  float minP = 0, maxP = 0, minT = 0, maxT = 0;
  uint32_t t = 10000;
  for (; t < 10000 + 120000 && m.sweeping(); t += 5) {
    m.update(t);
    float p = m.axis(PAN).position(), q = m.axis(TILT).position();
    minP = fminf(minP, p); maxP = fmaxf(maxP, p);
    minT = fminf(minT, q); maxT = fmaxf(maxT, q);
  }
  TEST_ASSERT_FALSE(m.sweeping());
  TEST_ASSERT_FLOAT_WITHIN(0.01, -88, minP);
  TEST_ASSERT_FLOAT_WITHIN(0.01, 88, maxP);
  TEST_ASSERT_FLOAT_WITHIN(0.01, -58, minT);
  TEST_ASSERT_FLOAT_WITHIN(0.01, 58, maxT);
  // a look cancels a sweep
  m.startSweep(t);
  m.look(0, 0, -1, t);
  TEST_ASSERT_FALSE(m.sweeping());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_default_soft_limits_inside_mechanical);
  RUN_TEST(test_clamps_target_to_soft_limits);
  RUN_TEST(test_position_never_exceeds_limits_during_overshoot);
  RUN_TEST(test_easing_endpoints);
  RUN_TEST(test_glance_no_anticipation_and_overshoot_bounded);
  RUN_TEST(test_overshoot_capped_in_degrees);
  RUN_TEST(test_duration_scales_with_distance);
  RUN_TEST(test_requested_duration_honoured_unless_too_fast);
  RUN_TEST(test_peak_speed_respects_limit);
  RUN_TEST(test_idle_detach_timing);
  RUN_TEST(test_idle_detach_survives_millis_wrap);
  RUN_TEST(test_idle_detach_disabled);
  RUN_TEST(test_pulse_trim_invert_and_clamp);
  RUN_TEST(test_look_syncs_axes_and_sweep_completes);
  return UNITY_END();
}
