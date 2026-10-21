// SPDX-License-Identifier: MIT
#include <math.h>
#include <unity.h>

#include "follow.h"
#include "motion.h"

using namespace lynx;

void setUp() {}
void tearDown() {}

static Target at(float cx, float cy, float size = 0.05f) {
  Target t;
  t.cx = cx;
  t.cy = cy;
  t.size = size;
  t.score = 0.9f;
  return t;
}

static void test_centred_target_does_not_move() {
  Follow f;
  Target t = at(0.02f, -0.03f);
  FollowMove m = f.onFrame(0, &t, 1, 0, 45);
  TEST_ASSERT_FALSE(m.move);  // inside the deadband
}

static void test_moves_toward_target_with_signs() {
  Follow f;
  Target right = at(0.5f, 0);  // right of centre
  FollowMove m = f.onFrame(0, &right, 1, 0, 45);
  TEST_ASSERT_TRUE(m.move);
  TEST_ASSERT_TRUE(m.pan < 0);  // pan + is the head's left; the right of the image is its right
  TEST_ASSERT_EQUAL_FLOAT(45, m.tilt);
  Target up = at(0, -0.5f);  // above centre -> toward lens-up (tilt decreases)
  m = f.onFrame(100, &up, 1, 0, 45);
  TEST_ASSERT_TRUE(m.tilt < 45);
  f.config().panSign = 1;  // mirrored image, or the board mounted the other way round
  m = f.onFrame(200, &right, 1, 0, 45);
  TEST_ASSERT_TRUE(m.pan > 0);
}

static void test_step_capped_and_clamped_to_limits() {
  Follow f;
  Target edge = at(1.0f, 0);  // 31 deg off: one update may move at most maxStepDeg
  FollowMove m = f.onFrame(0, &edge, 1, 0, 45);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, -12.f, m.pan);
  m = f.onFrame(100, &edge, 1, -85, 45);
  TEST_ASSERT_EQUAL_FLOAT(-88, m.pan);  // soft limit
  m = f.onFrame(200, &edge, 1, -88, 45);
  TEST_ASSERT_FALSE(m.move);  // pinned: nothing to do
}

static void test_picks_the_biggest_face() {
  Target t[3] = {at(-0.6f, 0, 0.01f), at(0.5f, 0, 0.09f), at(0.1f, 0.4f, 0.02f)};
  TEST_ASSERT_EQUAL(1, Follow::pick(t, 3));
  TEST_ASSERT_EQUAL(-1, Follow::pick(t, 0));
}

static void test_lost_target_holds_then_returns_to_rest() {
  Follow f;
  Target t = at(0.6f, 0);
  f.onFrame(1000, &t, 1, 0, 45);
  TEST_ASSERT_TRUE(f.tracking(1500));
  TEST_ASSERT_FALSE(f.onFrame(2000, nullptr, 0, 20, 45).move);  // empty frames: hold
  TEST_ASSERT_FALSE(f.tick(6000, 20, 45).move);
  TEST_ASSERT_FALSE(f.tracking(4000));
  FollowMove m = f.tick(7100, 20, 45);  // 6.1 s since last seen
  TEST_ASSERT_TRUE(m.move);
  TEST_ASSERT_TRUE(m.rest);
  TEST_ASSERT_EQUAL_FLOAT(0, m.pan);
  TEST_ASSERT_EQUAL_FLOAT(45, m.tilt);
  TEST_ASSERT_FALSE(f.tick(7200, 0, 45).move);  // only once
}

static void test_never_seen_does_nothing() {
  Follow f;
  TEST_ASSERT_FALSE(f.tick(100000, 30, 20).move);
}

// Closed loop: a face sits still at (30, 30) in head angles. Each detection frame, the camera sees it
// at the angular difference (signs as configured); the controller re-targets; the head gets there.
static void test_closed_loop_converges_without_overshoot() {
  Follow f;
  FollowConfig& c = f.config();
  const float facePan = 30, faceTilt = 30;
  float pan = 0, tilt = 45;
  float prevErr = 1e9f;
  for (int frame = 0; frame < 30; frame++) {
    // what the camera sees: right of centre if facePan > pan; above centre (cy < 0) if faceTilt < tilt
    Target t = at((facePan - pan) / (c.hfovDeg * 0.5f) * c.panSign, (faceTilt - tilt) / (c.vfovDeg * 0.5f) * c.tiltSign);
    FollowMove m = f.onFrame(frame * 200, &t, 1, pan, tilt);
    if (m.move) { pan = m.pan; tilt = m.tilt; }
    float err = fabsf(facePan - pan) + fabsf(faceTilt - tilt);
    TEST_ASSERT_TRUE(err <= prevErr + 1e-3f);  // monotonic: never swings past and back
    TEST_ASSERT_TRUE(pan <= facePan + 1e-3f && pan >= -1e-3f);  // never overshoots
    prevErr = err;
  }
  TEST_ASSERT_FLOAT_WITHIN(c.deadbandDeg * 2, facePan, pan);
  TEST_ASSERT_FLOAT_WITHIN(c.deadbandDeg * 2, faceTilt, tilt);
}

// Track moves: no overshoot, unlike glances
static void test_track_move_never_overshoots() {
  Motion m;
  m.begin(defaultAxisCal(PAN), defaultAxisCal(TILT), defaultMotionConfig(), 0);
  uint32_t t = 0;
  while (m.axis(PAN).moving() || m.axis(TILT).moving()) m.update(t += 5);
  uint32_t d = m.track(40, 45, 250, t);
  TEST_ASSERT_TRUE(d >= 250);
  float maxPos = 0;
  for (uint32_t e = 0; e <= d + 20; e += 5) {
    m.update(t + e);
    maxPos = fmaxf(maxPos, m.axis(PAN).position());
  }
  TEST_ASSERT_TRUE(maxPos <= 40.0001f);
  TEST_ASSERT_EQUAL_FLOAT(40, m.axis(PAN).position());
  // the glance does overshoot, as designed
  m.look(0, 45, -1, t + d + 50);
  uint32_t g0 = t + d + 50;
  float minPos = 100;
  for (uint32_t e = 0; e < 2000; e += 5) {
    m.update(g0 + e);
    minPos = fminf(minPos, m.axis(PAN).position());
  }
  TEST_ASSERT_TRUE(minPos < -0.5f);
}

static void test_track_leaves_at_speed() {
  // ease-out: most of the way there in the first half (feels responsive when chained)
  TEST_ASSERT_TRUE(easeOutCubic(0.5f) > 0.85f);
  TEST_ASSERT_EQUAL_FLOAT(0, easeOutCubic(0));
  TEST_ASSERT_EQUAL_FLOAT(1, easeOutCubic(1));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_centred_target_does_not_move);
  RUN_TEST(test_moves_toward_target_with_signs);
  RUN_TEST(test_step_capped_and_clamped_to_limits);
  RUN_TEST(test_picks_the_biggest_face);
  RUN_TEST(test_lost_target_holds_then_returns_to_rest);
  RUN_TEST(test_never_seen_does_nothing);
  RUN_TEST(test_closed_loop_converges_without_overshoot);
  RUN_TEST(test_track_move_never_overshoots);
  RUN_TEST(test_track_leaves_at_speed);
  return UNITY_END();
}
